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
inline float cutoffHz(float v) { return 20.0f * std::exp2(9.965784f * (v / 255.0f)); }
/// Inverse of cutoffHz, clamped to 0..255.
inline float cutoffValue(float hz) {
    const float v = std::log2(hz / 20.0f) * (255.0f / 9.965784f);
    return v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v);
}
/// Quadratic over the synth's Q range, giving finer steps at low resonance.
inline float resonanceQ(uint8_t v) {
    const float t = v / 255.0f;
    return synth::WavetableSynth::FILTER_Q_MIN +
           (synth::WavetableSynth::FILTER_Q_MAX - synth::WavetableSynth::FILTER_Q_MIN) * t * t;
}
inline float waveShape(uint8_t v) { return v / 255.0f; }
/// 4xy: x = 0..15 -> 0.5..8 Hz.
inline float vibratoRateHz(uint8_t x) { return 0.5f + x * 0.5f; }
/// 4xy: y = 0..15 -> 0..1.5 semitones.
inline float vibratoDepthSemitones(uint8_t y) { return y * 0.1f; }
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
        uint8_t tonePortaMemory = 0;  ///< Last nonzero 3xx parameter.
        /// 1xx/2xx: semitones per effect tick (0 = none). 3xx: glide speed
        /// toward `target` (0 = none). Both persist across rows until a new
        /// note, another pitch command, or (for a glide) reaching the target.
        float slide = 0.0f;
        float glide = 0.0f;
        uint8_t volSlideMemory = 0;   ///< Last nonzero Axy parameter.
        int8_t arpOffset[3] = {0, 0, 0};  ///< Semitones for arpeggio steps 0, 1, 2.
        /// 8xx/9xx: cutoff change per effect tick in Fxx steps (0 = no sweep).
        /// Persists across rows until another filter command or a restart.
        float filterSweep = 0.0f;
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
        for (auto& g : layerGains_) g.reset(1.0f);
    }

    /**
     * Play `sequence` (looping) from `startTick`; its entries index `patterns`.
     * Both must stay alive while playing, and every entry must be a valid
     * pattern (see Song::isPlayable). nullptr stops the channel; sounding
     * voices are released.
     */
    void playSequence(const Sequence* sequence, const Pattern* patterns, uint64_t startTick) {
        sequence_ = (sequence != nullptr && sequence->length > 0 && patterns != nullptr) ? sequence : nullptr;
        pool_ = patterns;
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
     * Musical mute, per column (bit c = column c): a muted column triggers no
     * new notes (note-offs still apply) and the sequence keeps its position,
     * so unmuting resumes in time. Muting releases sounding notes, which fade
     * out with their own release.
     */
    void setMutedColumns(uint32_t mask) {
        for (uint32_t c = 0; c < config::kMaxColumns; ++c) {
            if ((mask & ~mutedColumns_) & (1u << c)) voices_[c].release();
        }
        mutedColumns_ = mask;
    }
    void setMuted(bool muted) { setMutedColumns(muted ? kAllColumns : 0u); }
    uint32_t mutedColumns() const { return mutedColumns_; }
    /// True if every column is muted.
    bool isMuted() const { return (mutedColumns_ & kAllColumns) == kAllColumns; }

    /**
     * Columns whose gate is open (bit c = column c). A closed column starts
     * no new notes, but lets sounding ones finish; it comes back in with its
     * next note. Used for parameter thresholds (Song::setColumnGate).
     */
    void setOpenColumns(uint32_t mask) { openColumns_ = mask; }
    uint32_t openColumns() const { return openColumns_; }

    /// Columns that are heard (bit 0 = column 0). The others keep playing
    /// silently, so switching columns doesn't freeze them mid-note. Used to
    /// solo a column in the editor.
    void setColumnMask(uint32_t mask) { columnMask_ = mask; }
    uint32_t columnMask() const { return columnMask_; }

    /// How every column's notes are read.
    void setPitchMode(PitchMode mode) {
        for (auto& m : pitchModes_) m = mode;
    }
    void setPitchMode(uint32_t column, PitchMode mode) {
        if (column < config::kMaxColumns) pitchModes_[column] = mode;
    }
    PitchMode pitchMode(uint32_t column = 0) const {
        return column < config::kMaxColumns ? pitchModes_[column] : PitchMode::Absolute;
    }

    /// Strip gain (1 = unity), applied immediately.
    void setGain(float gain) { setGain(gain, 0); }
    /// Strip gain, gliding over `rampSamples` samples.
    void setGain(float gain, uint32_t rampSamples) {
        stripGain_ = gain;
        gainRamp_.set(stripGain_, rampSamples);
    }
    float gain() const { return stripGain_; }

    /// Combined gain of the layers a column belongs to; multiplies its voice.
    void setLayerGain(uint32_t column, float gain, uint32_t rampSamples) {
        if (column < config::kMaxColumns) layerGains_[column].set(gain, rampSamples);
    }
    /// The strip gain being applied right now (mid-glide if fading).
    float currentGain() const { return gainRamp_.value(); }
    /// A column's layer gain right now.
    float layerGain(uint32_t column) const { return column < config::kMaxColumns ? layerGains_[column].value() : 0.0f; }

    /// Channel low-pass filter cutoff in Hz, gliding over `rampSamples`.
    /// At or above kFilterBypassHz the filter is off.
    void setCutoff(float hz, uint32_t rampSamples) {
        if (hz < 20.0f) hz = 20.0f;
        if (hz > 20000.0f) hz = 20000.0f;
        cutoffLog2_.set(std::log2(hz), rampSamples / kCutoffBlock);
    }
    float cutoffHz() const { return std::exp2(cutoffLog2_.value()); }

    /// Number of columns the channel plays (1..kMaxColumns). Voices of
    /// columns it no longer plays are released.
    void setNumColumns(uint32_t n) {
        numColumns_ = n < 1 ? 1 : (n > config::kMaxColumns ? config::kMaxColumns : n);
        for (uint32_t c = numColumns_; c < config::kMaxColumns; ++c) voices_[c].release();
    }

    /// Release the voices of columns `from` onward (their cells moved).
    void releaseFrom(uint32_t from) {
        for (uint32_t c = from; c < config::kMaxColumns; ++c) voices_[c].release();
    }

    /**
     * Pattern `p` changed length or speed. If this channel is playing it,
     * playing continues from the current row at the new speed (or moves on, if
     * the pattern now ends before that row), and later entries of the sequence
     * keep their places. `tick` is the next tick to be processed.
     */
    void patternChanged(const Pattern* p, uint64_t tick) {
        if (!uses(p)) return;
        updateTotalTicks();
        const int64_t now = static_cast<int64_t>(tick);
        if (now <= startTick_ || playingRow_ < 0) return;  // not started: nothing to keep in place
        if (currentPattern() == p) keepRow(now);
        reanchor();
    }

    /// True if this channel is playing `s` (as a looping sequence).
    bool playsSequence(const Sequence* s) const { return sequence_ != nullptr && sequence_ == s; }

    /**
     * The sequence this channel plays was edited (and still has entries): the
     * pattern playing carries on in place. If it was taken out, the entry
     * now in its place starts at once; if it was replaced, the new pattern
     * carries on from the same row.
     */
    void sequenceEdited(SeqEdit op, uint32_t pos, uint64_t tick) {
        updateTotalTicks();
        const int64_t now = static_cast<int64_t>(tick);
        if (now <= startTick_ || playingRow_ < 0) return;
        switch (op) {
        case SeqEdit::Insert:
            if (pos <= entryIndex_) ++entryIndex_;
            break;
        case SeqEdit::Remove:
            if (pos < entryIndex_) {
                --entryIndex_;
            } else if (pos == entryIndex_) {
                entryIndex_ = pos < sequence_->length ? pos : 0;
                entryStartTick_ = now;
            }
            break;
        case SeqEdit::Set:
            if (pos == entryIndex_) keepRow(now);
            break;
        }
        reanchor();
    }

    /**
     * Pattern `p`'s rows were stretched or squashed (row r is now row
     * r * num / den): if it is playing, carry on from where the current row
     * went, so the music keeps its place.
     */
    void rowsScaled(const Pattern* p, uint32_t num, uint32_t den, uint64_t tick) {
        if (!uses(p)) return;
        if (currentPattern() == p && playingRow_ >= 0) {
            playingRow_ = static_cast<int32_t>(static_cast<uint32_t>(playingRow_) * num / den);
        }
        patternChanged(p, tick);
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
        // A tick is processed at most once, unless playback was (re)started
        // since, e.g. by a section change fired from another channel's row.
        if (tick == lastTick_) return;
        lastTick_ = tick;
        numFiredEvents_ = 0;

        const Pattern* pattern = currentPattern();
        const int64_t now = static_cast<int64_t>(tick);
        if (pattern == nullptr || now < startTick_) return;

        if (static_cast<uint64_t>(now - entryStartTick_) >= pattern->lengthTicks()) {
            if (!locate(tick)) return;  // a one-shot pattern finished
            pattern = currentPattern();
        }

        const uint32_t ticksPerRow = pattern->engineTicksPerRow();
        const uint64_t tickInPattern = static_cast<uint64_t>(now - entryStartTick_);
        if (tickInPattern % ticksPerRow == 0) {
            const uint32_t row = static_cast<uint32_t>(tickInPattern / ticksPerRow);
            playingRow_ = static_cast<int32_t>(row);
            rowStartTick_ = now;
            for (uint32_t c = 0; c < numColumns_; ++c) startRow(c, pattern->cells[row][c], pitch);
        } else if (tick % config::kTicksPerEffectTick == 0) {
            for (uint32_t c = 0; c < numColumns_; ++c) effectTick(c);
        }
    }

    /// Events fired by Bxx effects on the tick just processed.
    uint32_t numFiredEvents() const { return numFiredEvents_; }
    uint8_t firedEvent(uint32_t i) const { return firedEvents_[i]; }
    void clearFiredEvents() { numFiredEvents_ = 0; }

    /// The pattern playing now (nullptr if none) and the last row that started in it.
    const Pattern* playingPattern() const { return currentPattern(); }
    int32_t playingRow() const { return currentPattern() != nullptr ? playingRow_ : -1; }

    /// Add `numFrames` samples of this channel into `mix`. If `tap` is set,
    /// the voices of the columns in `tapColumns` that are heard are also
    /// added into it, before the channel's filter and gain.
    void render(float* mix, uint32_t numFrames, float* tap = nullptr, uint32_t tapColumns = 0) {
        if (tap == nullptr) tapColumns = 0;
        while (numFrames > 0) {
            const uint32_t n = numFrames < kScratchFrames ? numFrames : kScratchFrames;
            renderBlock(mix, n, tap, tapColumns);
            mix += n;
            if (tap != nullptr) tap += n;
            numFrames -= n;
        }
    }

private:
    static constexpr uint32_t kScratchFrames = 64;
    /// The filter cutoff moves once per this many samples (counted from the
    /// start of playback, so output does not depend on audio buffer sizes).
    static constexpr uint32_t kCutoffBlock = 16;

    void renderBlock(float* mix, uint32_t n, float* tap, uint32_t tapColumns) {
        bool any = false;
        for (uint32_t c = 0; c < numColumns_; ++c) any = any || voices_[c].isActive();
        if (!any) {
            gainRamp_.advance(n);
            for (auto& g : layerGains_) g.advance(n);
            advanceCutoff(n);
            return;
        }

        for (uint32_t i = 0; i < n; ++i) scratch_[i] = 0.0f;
        for (uint32_t c = 0; c < config::kMaxColumns; ++c) {
            synth::WavetableSynth& v = voices_[c];
            Ramp& g = layerGains_[c];
            if (c >= numColumns_ || !v.isActive()) {
                g.advance(n);
                continue;
            }
            if ((columnMask_ & tapColumns) & (1u << c)) {
                for (uint32_t i = 0; i < n; ++i) {
                    const float x = v.nextSample(timer_) * g.next();
                    scratch_[i] += x;
                    tap[i] += x;
                }
            } else if (columnMask_ & (1u << c)) {
                for (uint32_t i = 0; i < n; ++i) scratch_[i] += v.nextSample(timer_) * g.next();
            } else {
                g.advance(n);
                for (uint32_t i = 0; i < n; ++i) v.nextSample(timer_);  // keep time, stay silent
            }
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
        return sequence_ != nullptr ? &pool_[sequence_->entries[entryIndex_]] : single_;
    }

    /// Entry i of what plays (the single pattern, when not a sequence).
    const Pattern& entryPattern(uint32_t i) const {
        return sequence_ != nullptr ? pool_[sequence_->entries[i]] : *single_;
    }

    bool uses(const Pattern* p) const {
        if (single_ != nullptr) return single_ == p;
        if (sequence_ == nullptr) return false;
        for (uint32_t i = 0; i < sequence_->length; ++i) {
            if (&pool_[sequence_->entries[i]] == p) return true;
        }
        return false;
    }

    void updateTotalTicks() {
        totalTicks_ = 0;
        const uint32_t entries = sequence_ != nullptr ? sequence_->length : 1;
        for (uint32_t i = 0; i < entries; ++i) totalTicks_ += entryPattern(i).lengthTicks();
    }

    /// The current entry's pattern changed shape (or is another pattern):
    /// carry on from the row playing, or end the entry now if that row is gone.
    void keepRow(int64_t now) {
        const Pattern* p = currentPattern();
        if (static_cast<uint32_t>(playingRow_) >= p->numRows) {
            entryStartTick_ = now - static_cast<int64_t>(p->lengthTicks());  // ends now
        } else {
            entryStartTick_ = rowStartTick_ - int64_t(playingRow_) * int64_t(p->engineTicksPerRow());
        }
    }

    /// Where the sequence would have started for the current entry to start where it did.
    void reanchor() {
        int64_t before = 0;
        for (uint32_t i = 0; i < entryIndex_; ++i) before += entryPattern(i).lengthTicks();
        startTick_ = entryStartTick_ - before;
    }

    void restart(uint64_t startTick) {
        lastTick_ = kNoTick;  // allow this tick to be processed again
        playingRow_ = -1;
        startTick_ = static_cast<int64_t>(startTick);
        entryStartTick_ = startTick_;
        entryIndex_ = 0;
        totalTicks_ = 0;
        for (auto& c : columns_) c.filterSweep = c.slide = c.glide = 0.0f;
        if (sequence_ != nullptr) {
            for (uint32_t i = 0; i < sequence_->length; ++i) totalTicks_ += pool_[sequence_->entries[i]].lengthTicks();
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
        uint64_t offset = static_cast<uint64_t>(static_cast<int64_t>(tick) - startTick_);
        if (offset >= totalTicks_) {
            if (!loop_) {
                sequence_ = nullptr;
                single_ = nullptr;
                for (auto& v : voices_) v.release();
                return false;
            }
        }
        const int64_t loopStart = startTick_ + static_cast<int64_t>((offset / totalTicks_) * totalTicks_);
        offset %= totalTicks_;
        int64_t entryStart = loopStart;
        uint32_t index = 0;
        if (sequence_ != nullptr) {
            while (offset >= pool_[sequence_->entries[index]].lengthTicks()) {
                const uint32_t len = pool_[sequence_->entries[index]].lengthTicks();
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

        // A sweep set on an earlier row keeps stepping on row starts too, so
        // it moves evenly across rows, unless this row has a new filter command.
        if (cell.effect != fx::kFilterCutoff && cell.effect != fx::kFilterSweepUp &&
            cell.effect != fx::kFilterSweepDown) {
            sweepFilter(s, v);
        }
        // Pitch motion from earlier rows also continues on row starts, unless
        // this row has a new note or pitch command, which ends it.
        const bool pitchCommand = cell.effect == fx::kPortaUp || cell.effect == fx::kPortaDown ||
                                  cell.effect == fx::kTonePorta;
        if (pitchCommand || cell.isNote()) {
            s.slide = s.glide = 0.0f;
        } else {
            stepPitch(s);
        }
        s.effect = cell.effect;
        s.effectParam = cell.effectParam;
        s.effectTickInRow = 0;
        switch (cell.effect) {
        case fx::kPortaUp:
            s.slide = cell.effectParam / 16.0f;
            break;
        case fx::kPortaDown:
            s.slide = -cell.effectParam / 16.0f;
            break;
        case fx::kTonePorta:
            if (cell.effectParam != 0) s.tonePortaMemory = cell.effectParam;
            s.glide = s.tonePortaMemory / 16.0f;
            break;
        case fx::kVolumeSlide:
            if (cell.effectParam != 0) s.volSlideMemory = cell.effectParam;
            break;
        default:
            break;
        }

        if (cell.isNote() && plays(c)) {
            const float p = static_cast<float>(resolvePitch(pitchModes_[c], cell.note, cell.accidental, ctx));
            s.lastNote = cell.note;
            s.lastAccidental = cell.accidental;
            if (cell.effect == fx::kTonePorta && v.isActive()) {
                s.target = p;
            } else {
                s.pitch = s.target = p;
                s.volume = cell.volume == Cell::kVolumeDefault ? 1.0f : cell.volume / 127.0f;
                v.trigger(midiNoteToHz(p), s.volume);
            }
        } else if (cell.isOff() || cell.isNote()) {
            // A note-off, or a note this column may not play (gate closed or
            // muted): what was sounding ends here, on the note's boundary.
            v.release();
        }

        if (cell.volume != Cell::kVolumeDefault) {
            s.volume = cell.volume / 127.0f;
            v.setVolume(s.volume);
        }

        switch (cell.effect) {
        case fx::kArpeggio:
            computeArpeggio(s, pitchModes_[c], ctx);
            break;
        case fx::kFilterCutoff:
            v.setBaseCutoff(fxmap::cutoffHz(cell.effectParam));
            s.filterSweep = 0.0f;
            break;
        case fx::kFilterSweepUp:
            s.filterSweep = cell.effectParam / 16.0f;
            break;
        case fx::kFilterSweepDown:
            s.filterSweep = -cell.effectParam / 16.0f;
            break;
        case fx::kFilterResonance:
            v.setFilterQ(fxmap::resonanceQ(cell.effectParam));
            break;
        case fx::kWaveShape:
            v.setWaveformShape(fxmap::waveShape(cell.effectParam));
            break;
        case fx::kVibrato:
            v.setVibratoRate(fxmap::vibratoRateHz(cell.effectParam >> 4));
            v.setVibratoDepth(fxmap::vibratoDepthSemitones(cell.effectParam & 0x0F));
            break;
        case fx::kSetVolume:
            s.volume = (cell.effectParam > 0x7F ? 0x7F : cell.effectParam) / 127.0f;
            v.setVolume(s.volume);
            break;
        case fx::kFireEvent:
            if (numFiredEvents_ < config::kMaxColumns) firedEvents_[numFiredEvents_++] = cell.effectParam;
            break;
        default:
            break;
        }
        v.setFrequency(midiNoteToHz(s.soundingPitch()));
    }

    /// Arpeggio offsets from the column's last note. In Degree mode x and y are
    /// scale steps, so 0x24 plays a triad of whatever scale is current.
    void computeArpeggio(ColumnState& s, PitchMode mode, const PitchContext& ctx) const {
        const int x = s.effectParam >> 4;
        const int y = s.effectParam & 0x0F;
        s.arpOffset[0] = 0;
        if (mode == PitchMode::Degree && s.lastNote <= 127) {
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

    /// One 8xx/9xx step. Stops at either end of the range.
    static void sweepFilter(ColumnState& s, synth::WavetableSynth& v) {
        if (s.filterSweep == 0.0f) return;
        const float now = fxmap::cutoffValue(v.getBaseCutoff()) + s.filterSweep;
        if (now <= 0.0f || now >= 255.0f) s.filterSweep = 0.0f;
        v.setBaseCutoff(fxmap::cutoffHz(now < 0.0f ? 0.0f : (now > 255.0f ? 255.0f : now)));
    }

    /// One step of 1xx/2xx slide or 3xx glide.
    static void stepPitch(ColumnState& s) {
        if (s.slide != 0.0f) {
            s.pitch += s.slide;
            if (s.pitch > 127.0f || s.pitch < 0.0f) {
                s.pitch = s.pitch > 127.0f ? 127.0f : 0.0f;
                s.slide = 0.0f;
            }
        }
        if (s.glide != 0.0f) {
            if (s.pitch < s.target) {
                s.pitch = (s.pitch + s.glide > s.target) ? s.target : s.pitch + s.glide;
            } else if (s.pitch > s.target) {
                s.pitch = (s.pitch - s.glide < s.target) ? s.target : s.pitch - s.glide;
            }
            if (s.pitch == s.target) s.glide = 0.0f;
        }
    }

    void effectTick(uint32_t c) {
        ColumnState& s = columns_[c];
        synth::WavetableSynth& v = voices_[c];
        ++s.effectTickInRow;

        sweepFilter(s, v);
        const bool moving = s.slide != 0.0f || s.glide != 0.0f;
        stepPitch(s);
        if (moving || s.effect == fx::kArpeggio) v.setFrequency(midiNoteToHz(s.soundingPitch()));

        if (s.effect == fx::kVolumeSlide) {
            const int up = s.volSlideMemory >> 4;
            const int down = s.volSlideMemory & 0x0F;
            s.volume += (up != 0 ? up : -down) / 64.0f;
            if (s.volume < 0.0f) s.volume = 0.0f;
            if (s.volume > 1.0f) s.volume = 1.0f;
            v.setVolume(s.volume);
        }
    }

    std::array<synth::WavetableSynth, config::kMaxColumns> voices_;
    std::array<ColumnState, config::kMaxColumns> columns_{};
    Timer timer_;
    const Sequence* sequence_ = nullptr;
    const Pattern* pool_ = nullptr;  // what sequence_ entries index
    const Pattern* single_ = nullptr;
    // Signed: after an edit makes rows longer, the current entry can have
    // "started" before tick 0 (see patternChanged).
    int64_t startTick_ = 0;
    int64_t entryStartTick_ = 0;
    int64_t rowStartTick_ = 0;  ///< When playingRow_ started.
    static constexpr uint64_t kNoTick = ~uint64_t(0);
    uint64_t lastTick_ = kNoTick;
    int32_t playingRow_ = -1;
    uint8_t firedEvents_[config::kMaxColumns] = {};
    uint32_t numFiredEvents_ = 0;
    uint64_t totalTicks_ = 0;  ///< Length of the whole sequence (or single pattern).
    uint32_t entryIndex_ = 0;
    uint32_t numColumns_ = 1;
    bool loop_ = true;
    static constexpr uint32_t kAllColumns = (1u << config::kMaxColumns) - 1u;
    /// Starts new notes: not muted, and its gate is open.
    bool plays(uint32_t c) const { return ((openColumns_ & ~mutedColumns_) >> c) & 1u; }
    uint32_t mutedColumns_ = 0;
    uint32_t openColumns_ = ~0u;
    uint32_t columnMask_ = ~0u;
    PitchMode pitchModes_[config::kMaxColumns] = {};

    // Channel strip.
    synth::BiquadFilter filter_;
    Ramp gainRamp_;
    Ramp cutoffLog2_;  ///< log2(cutoff Hz); moves once per kCutoffBlock samples
    uint32_t cutoffPhase_ = 0;
    float stripGain_ = 1.0f;
    Ramp layerGains_[config::kMaxColumns];  ///< 1 until a layer gain mapping says otherwise
    float scratch_[kScratchFrames] = {};
};

} // namespace adaptive
