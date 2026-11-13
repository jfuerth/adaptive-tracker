#pragma once

#include <adaptive/config.hpp>
#include <adaptive/pattern.hpp>
#include <adaptive/pitch.hpp>
#include <adaptive/ramp.hpp>

#include <biquad_filter.hpp>

#include <sawtooth_synth.hpp>
#include <performance_timer.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

namespace adaptive {

namespace detail {
template<std::size_t... I>
std::array<synth::WavetableSynth, sizeof...(I)> makeVoices(float sampleRate, std::index_sequence<I...>) {
    return {{(static_cast<void>(I), synth::WavetableSynth(sampleRate))...}};
}
} // namespace detail

/// Pitch in (fractional) MIDI note numbers to frequency in Hz (A4 = 440 Hz).
inline float midiNoteToHz(float note) {
    return 440.0f * std::exp2((note - 69.0f) / 12.0f);
}

/// Parameter mappings for the synth-parameter effects (Fxx, Qxx, Wxx).
namespace fxmap {
/// 00 = 20 Hz, FF = 20 kHz, exponential.
inline float cutoffHz(uint8_t v) { return 20.0f * std::exp2(9.965784f * (v / 255.0f)); }
/// Quadratic over the synth's Q range, giving finer steps at low resonance.
inline float resonanceQ(uint8_t v) {
    const float t = v / 255.0f;
    return synth::WavetableSynth::FILTER_Q_MIN +
           (synth::WavetableSynth::FILTER_Q_MAX - synth::WavetableSynth::FILTER_Q_MIN) * t * t;
}
inline float waveShape(uint8_t v) { return v / 255.0f; }
} // namespace fxmap

/**
 * One mixer strip. Its source is a sequence of patterns; each pattern column
 * drives one pressense voice.
 *
 * Effects follow tracker conventions: notes, volume and parameter effects apply
 * at the start of a row; slides and arpeggios update on every later effect tick
 * of the row (config::kTicksPerEffectTick). Musical mute, channel effects and
 * adaptive modulation arrive with the adaptive director (milestone 4).
 */
class Channel {
public:
    using Timer = features::LapTimer<features::NoOpTimingPolicy, 1>;

    /// Per-column playback state, exposed read-only for tests and UI meters.
    struct ColumnState {
        float pitch = 60.0f;   ///< Current pitch in MIDI notes, excluding arpeggio.
        float target = 60.0f;  ///< Tone portamento target.
        float volume = 1.0f;   ///< 0..1
        uint8_t effect = fx::kNone;
        uint8_t effectParam = 0;
        uint8_t portaMemory = 0;      ///< Last nonzero 1xx/2xx parameter.
        uint8_t tonePortaMemory = 0;  ///< Last nonzero 3xx parameter.
        uint8_t volSlideMemory = 0;   ///< Last nonzero Axy parameter.
        int8_t arpOffset[3] = {0, 0, 0};  ///< Semitones for arpeggio steps 0, 1, 2.
        uint8_t lastNote = Cell::kNoteEmpty;
        int8_t lastAccidental = 0;
        uint16_t effectTickInRow = 0;

        /// Pitch the voice is sounding now, including the arpeggio step.
        float soundingPitch() const {
            return effect == fx::kArpeggio ? pitch + arpOffset[effectTickInRow % 3] : pitch;
        }
    };

    /// Channel filter cutoffs at or above this are treated as "filter off".
    static constexpr float kFilterBypassHz = 18000.0f;

    explicit Channel(float sampleRate)
        : voices_(detail::makeVoices(sampleRate, std::make_index_sequence<config::kMaxColumns>{})),
          filter_(sampleRate),
          gainRamp_(1.0f),
          cutoffLog2_(std::log2(20000.0f)) {
        filter_.setMode(synth::BiquadFilter::Mode::LOWPASS);
        filter_.setQ(0.707f);
        filter_.setCutoff(20000.0f);
    }

    /**
     * Play `sequence` (looping) from `startTick`. It and its patterns must stay
     * alive while playing. nullptr stops the channel; sounding voices are released.
     */
    void playSequence(const Sequence* sequence, uint64_t startTick) {
        sequence_ = (sequence != nullptr && sequence->length > 0) ? sequence : nullptr;
        single_ = nullptr;
        loop_ = true;
        restart(startTick);
    }

    /// Play a single pattern (looping) from `startTick`.
    void play(const Pattern* pattern, uint64_t startTick) {
        sequence_ = nullptr;
        single_ = (pattern != nullptr && pattern->isValid()) ? pattern : nullptr;
        loop_ = true;
        restart(startTick);
    }

    /// Play a pattern once from `startTick`, then stop (used for stingers).
    void playOnce(const Pattern* pattern, uint64_t startTick) {
        play(pattern, startTick);
        loop_ = false;
    }

    /// True while a sequence or pattern is assigned (including a stinger that
    /// hasn't finished).
    bool isPlaying() const { return currentPattern() != nullptr; }
    /// True while playing a one-shot pattern (a stinger).
    bool isOneShot() const { return isPlaying() && !loop_; }

    /**
     * Musical mute: while muted, rows trigger no new notes (note-offs still
     * apply) and the sequence keeps its position, so unmuting resumes in time.
     * Muting releases sounding notes, which fade out with their own release.
     */
    void setMuted(bool muted) {
        if (muted && !muted_) {
            for (auto& v : voices_) v.release();
        }
        muted_ = muted;
    }
    bool isMuted() const { return muted_; }

    void setPitchMode(PitchMode mode) { pitchMode_ = mode; }
    PitchMode pitchMode() const { return pitchMode_; }

    /// Strip gain (1 = unity), applied immediately.
    void setGain(float gain) { setGain(gain, 0); }
    /// Strip gain, gliding over `rampSamples` samples.
    void setGain(float gain, uint32_t rampSamples) {
        stripGain_ = gain;
        gainRamp_.set(stripGain_ * layerGain_, rampSamples);
    }
    float gain() const { return stripGain_; }

    /// Combined gain of the layers this channel belongs to; multiplies the strip gain.
    void setLayerGain(float gain, uint32_t rampSamples) {
        layerGain_ = gain;
        gainRamp_.set(stripGain_ * layerGain_, rampSamples);
    }
    /// The gain being applied right now (strip x layers, mid-glide if fading).
    float currentGain() const { return gainRamp_.value(); }

    /// Channel low-pass filter cutoff in Hz, gliding over `rampSamples`.
    /// At or above kFilterBypassHz the filter is off.
    void setCutoff(float hz, uint32_t rampSamples) {
        if (hz < 20.0f) hz = 20.0f;
        if (hz > 20000.0f) hz = 20000.0f;
        cutoffLog2_.set(std::log2(hz), rampSamples / kCutoffBlock);
    }
    float cutoffHz() const { return std::exp2(cutoffLog2_.value()); }

    /// Number of columns the channel plays (1..kMaxColumns).
    void setNumColumns(uint32_t n) {
        numColumns_ = n < 1 ? 1 : (n > config::kMaxColumns ? config::kMaxColumns : n);
    }
    uint32_t numColumns() const { return numColumns_; }

    synth::WavetableSynth& voice(uint32_t column) { return voices_[column]; }
    const ColumnState& column(uint32_t c) const { return columns_[c]; }

    /// Index of the sequence entry now playing.
    uint32_t sequencePosition() const { return entryIndex_; }

    /// Apply a patch-setting function to every voice in the channel.
    template<typename Fn>
    void forEachVoice(Fn&& fn) {
        for (auto& v : voices_) fn(v);
    }

    /// Called by the engine at the start of every tick.
    void onTick(uint64_t tick, const PitchContext& pitch) {
        const Pattern* pattern = currentPattern();
        if (pattern == nullptr || tick < startTick_) return;

        if (tick - entryStartTick_ >= pattern->lengthTicks()) {
            if (!locate(tick)) return;  // a one-shot pattern finished
            pattern = currentPattern();
        }

        const uint32_t ticksPerRow = pattern->ticksPerRow();
        const uint64_t tickInPattern = tick - entryStartTick_;
        if (tickInPattern % ticksPerRow == 0) {
            const uint32_t row = static_cast<uint32_t>(tickInPattern / ticksPerRow);
            for (uint32_t c = 0; c < numColumns_; ++c) startRow(c, pattern->cells[row][c], pitch);
        } else if (tick % config::kTicksPerEffectTick == 0) {
            for (uint32_t c = 0; c < numColumns_; ++c) effectTick(c);
        }
    }

    /// Add `numFrames` samples of this channel into `mix`.
    void render(float* mix, uint32_t numFrames) {
        while (numFrames > 0) {
            const uint32_t n = numFrames < kScratchFrames ? numFrames : kScratchFrames;
            renderBlock(mix, n);
            mix += n;
            numFrames -= n;
        }
    }

private:
    static constexpr uint32_t kScratchFrames = 64;
    /// The filter cutoff moves once per this many samples (counted from the
    /// start of playback, so output does not depend on audio buffer sizes).
    static constexpr uint32_t kCutoffBlock = 16;

    void renderBlock(float* mix, uint32_t n) {
        bool any = false;
        for (uint32_t c = 0; c < numColumns_; ++c) any = any || voices_[c].isActive();
        if (!any) {
            gainRamp_.advance(n);
            advanceCutoff(n);
            return;
        }

        for (uint32_t i = 0; i < n; ++i) scratch_[i] = 0.0f;
        for (uint32_t c = 0; c < numColumns_; ++c) {
            synth::WavetableSynth& v = voices_[c];
            if (!v.isActive()) continue;
            for (uint32_t i = 0; i < n; ++i) scratch_[i] += v.nextSample(timer_);
        }

        const bool filterOn = cutoffLog2_.isMoving() || cutoffLog2_.value() < kBypassLog2;
        for (uint32_t i = 0; i < n; ++i) {
            if (++cutoffPhase_ == kCutoffBlock) {
                cutoffPhase_ = 0;
                cutoffLog2_.next();
                if (filterOn) filter_.setCutoff(std::exp2(cutoffLog2_.value()));
            }
            const float x = filterOn ? filter_.processSample(scratch_[i]) : scratch_[i];
            mix[i] += x * gainRamp_.next();
        }
    }

    void advanceCutoff(uint32_t n) {
        const uint32_t total = cutoffPhase_ + n;
        cutoffLog2_.advance(total / kCutoffBlock);
        cutoffPhase_ = total % kCutoffBlock;
    }

    static inline const float kBypassLog2 = std::log2(kFilterBypassHz);

    const Pattern* currentPattern() const {
        return sequence_ != nullptr ? sequence_->entries[entryIndex_] : single_;
    }

    void restart(uint64_t startTick) {
        startTick_ = startTick;
        entryStartTick_ = startTick;
        entryIndex_ = 0;
        totalTicks_ = 0;
        if (sequence_ != nullptr) {
            for (uint32_t i = 0; i < sequence_->length; ++i) totalTicks_ += sequence_->entries[i]->lengthTicks();
        } else if (single_ != nullptr) {
            totalTicks_ = single_->lengthTicks();
        } else {
            for (auto& v : voices_) v.release();
        }
    }

    /**
     * Find the sequence entry that contains `tick`. Handles any distance from
     * the start, including a start in the past (sections entered with
     * Entry::KeepPhase). Returns false, and stops the channel, when a one-shot
     * pattern has finished.
     */
    bool locate(uint64_t tick) {
        uint64_t offset = tick - startTick_;
        if (offset >= totalTicks_) {
            if (!loop_) {
                sequence_ = nullptr;
                single_ = nullptr;
                for (auto& v : voices_) v.release();
                return false;
            }
        }
        const uint64_t loopStart = startTick_ + (offset / totalTicks_) * totalTicks_;
        offset %= totalTicks_;
        uint64_t entryStart = loopStart;
        uint32_t index = 0;
        if (sequence_ != nullptr) {
            while (offset >= sequence_->entries[index]->lengthTicks()) {
                const uint32_t len = sequence_->entries[index]->lengthTicks();
                offset -= len;
                entryStart += len;
                ++index;
            }
        }
        entryIndex_ = index;
        entryStartTick_ = entryStart;
        return true;
    }

    void startRow(uint32_t c, const Cell& cell, const PitchContext& ctx) {
        ColumnState& s = columns_[c];
        synth::WavetableSynth& v = voices_[c];

        s.effect = cell.effect;
        s.effectParam = cell.effectParam;
        s.effectTickInRow = 0;
        switch (cell.effect) {
        case fx::kPortaUp:
        case fx::kPortaDown:
            if (cell.effectParam != 0) s.portaMemory = cell.effectParam;
            break;
        case fx::kTonePorta:
            if (cell.effectParam != 0) s.tonePortaMemory = cell.effectParam;
            break;
        case fx::kVolumeSlide:
            if (cell.effectParam != 0) s.volSlideMemory = cell.effectParam;
            break;
        default:
            break;
        }

        if (cell.isNote() && !muted_) {
            const float p = static_cast<float>(resolvePitch(pitchMode_, cell.note, cell.accidental, ctx));
            s.lastNote = cell.note;
            s.lastAccidental = cell.accidental;
            if (cell.effect == fx::kTonePorta && v.isActive()) {
                s.target = p;
            } else {
                s.pitch = s.target = p;
                s.volume = cell.volume == Cell::kVolumeDefault ? 1.0f : cell.volume / 127.0f;
                v.trigger(midiNoteToHz(p), s.volume);
            }
        } else if (cell.isOff()) {
            v.release();
        }

        if (cell.volume != Cell::kVolumeDefault) {
            s.volume = cell.volume / 127.0f;
            v.setVolume(s.volume);
        }

        switch (cell.effect) {
        case fx::kArpeggio:
            computeArpeggio(s, ctx);
            break;
        case fx::kFilterCutoff:
            v.setBaseCutoff(fxmap::cutoffHz(cell.effectParam));
            break;
        case fx::kFilterResonance:
            v.setFilterQ(fxmap::resonanceQ(cell.effectParam));
            break;
        case fx::kWaveShape:
            v.setWaveformShape(fxmap::waveShape(cell.effectParam));
            break;
        default:
            break;
        }
        v.setFrequency(midiNoteToHz(s.soundingPitch()));
    }

    /// Arpeggio offsets from the column's last note. In Degree mode x and y are
    /// scale steps, so 0x24 plays a triad of whatever scale is current.
    void computeArpeggio(ColumnState& s, const PitchContext& ctx) const {
        const int x = s.effectParam >> 4;
        const int y = s.effectParam & 0x0F;
        s.arpOffset[0] = 0;
        if (pitchMode_ == PitchMode::Degree && s.lastNote <= 127) {
            const int octave = s.lastNote / 12;
            const int degree = s.lastNote % 12;
            const int base = resolveDegree(octave, degree, s.lastAccidental, ctx);
            s.arpOffset[1] = static_cast<int8_t>(resolveDegree(octave, degree + x, s.lastAccidental, ctx) - base);
            s.arpOffset[2] = static_cast<int8_t>(resolveDegree(octave, degree + y, s.lastAccidental, ctx) - base);
        } else {
            s.arpOffset[1] = static_cast<int8_t>(x);
            s.arpOffset[2] = static_cast<int8_t>(y);
        }
    }

    void effectTick(uint32_t c) {
        ColumnState& s = columns_[c];
        synth::WavetableSynth& v = voices_[c];
        ++s.effectTickInRow;

        switch (s.effect) {
        case fx::kArpeggio:
            break;  // soundingPitch() steps through the offsets
        case fx::kPortaUp:
            s.pitch += s.portaMemory / 16.0f;
            if (s.pitch > 127.0f) s.pitch = 127.0f;
            break;
        case fx::kPortaDown:
            s.pitch -= s.portaMemory / 16.0f;
            if (s.pitch < 0.0f) s.pitch = 0.0f;
            break;
        case fx::kTonePorta: {
            const float step = s.tonePortaMemory / 16.0f;
            if (s.pitch < s.target) {
                s.pitch = (s.pitch + step > s.target) ? s.target : s.pitch + step;
            } else if (s.pitch > s.target) {
                s.pitch = (s.pitch - step < s.target) ? s.target : s.pitch - step;
            }
            break;
        }
        case fx::kVolumeSlide: {
            const int up = s.volSlideMemory >> 4;
            const int down = s.volSlideMemory & 0x0F;
            s.volume += (up != 0 ? up : -down) / 64.0f;
            if (s.volume < 0.0f) s.volume = 0.0f;
            if (s.volume > 1.0f) s.volume = 1.0f;
            v.setVolume(s.volume);
            return;
        }
        default:
            return;
        }
        v.setFrequency(midiNoteToHz(s.soundingPitch()));
    }

    std::array<synth::WavetableSynth, config::kMaxColumns> voices_;
    std::array<ColumnState, config::kMaxColumns> columns_{};
    Timer timer_;
    const Sequence* sequence_ = nullptr;
    const Pattern* single_ = nullptr;
    uint64_t startTick_ = 0;
    uint64_t entryStartTick_ = 0;
    uint64_t totalTicks_ = 0;  ///< Length of the whole sequence (or single pattern).
    uint32_t entryIndex_ = 0;
    uint32_t numColumns_ = 1;
    bool loop_ = true;
    bool muted_ = false;
    PitchMode pitchMode_ = PitchMode::Absolute;

    // Channel strip.
    synth::BiquadFilter filter_;
    Ramp gainRamp_;
    Ramp cutoffLog2_;  ///< log2(cutoff Hz); moves once per kCutoffBlock samples
    uint32_t cutoffPhase_ = 0;
    float stripGain_ = 1.0f;
    float layerGain_ = 1.0f;
    float scratch_[kScratchFrames] = {};
};

} // namespace adaptive
