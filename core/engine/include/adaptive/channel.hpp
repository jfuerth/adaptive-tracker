#pragma once

#include <adaptive/config.hpp>
#include <adaptive/pattern.hpp>

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

/// MIDI note number to frequency in Hz (A4 = 440 Hz, equal temperament).
inline float midiNoteToHz(uint8_t note) {
    return 440.0f * std::exp2((static_cast<float>(note) - 69.0f) / 12.0f);
}

/**
 * One mixer strip. Its source is a pattern; each pattern column drives one
 * pressense voice.
 *
 * Milestone 1: a single looping pattern and a gain. Pattern sequences, channel
 * effects and musical mute arrive with the sequencer and adaptive director.
 */
class Channel {
public:
    using Timer = features::LapTimer<features::NoOpTimingPolicy, 1>;

    explicit Channel(float sampleRate)
        : voices_(detail::makeVoices(sampleRate, std::make_index_sequence<config::kMaxColumns>{})) {}

    /**
     * Start playing `pattern` (looping) from `startTick`. The pattern must stay
     * alive while it is playing. Passing nullptr stops the channel's sequence;
     * voices already sounding are released.
     */
    void play(const Pattern* pattern, uint64_t startTick) {
        pattern_ = (pattern != nullptr && pattern->isValid()) ? pattern : nullptr;
        startTick_ = startTick;
        if (pattern_ == nullptr) releaseAll();
    }

    void setGain(float gain) { gain_ = gain; }
    float gain() const { return gain_; }

    /// Number of columns the channel plays (1..kMaxColumns).
    void setNumColumns(uint32_t n) {
        numColumns_ = n < 1 ? 1 : (n > config::kMaxColumns ? config::kMaxColumns : n);
    }
    uint32_t numColumns() const { return numColumns_; }

    synth::WavetableSynth& voice(uint32_t column) { return voices_[column]; }

    /// Apply a patch-setting function to every voice in the channel.
    template<typename Fn>
    void forEachVoice(Fn&& fn) {
        for (auto& v : voices_) fn(v);
    }

    /// Called by the engine at the start of every tick.
    void onTick(uint64_t tick) {
        if (pattern_ == nullptr || tick < startTick_) return;
        const uint32_t ticksPerRow = pattern_->ticksPerRow();
        const uint64_t tickInPattern = (tick - startTick_) % pattern_->lengthTicks();
        if (tickInPattern % ticksPerRow != 0) return;
        triggerRow(static_cast<uint32_t>(tickInPattern / ticksPerRow));
    }

    /// Add `numFrames` samples of this channel into `mix`.
    void render(float* mix, uint32_t numFrames) {
        for (uint32_t c = 0; c < numColumns_; ++c) {
            synth::WavetableSynth& v = voices_[c];
            if (!v.isActive()) continue;
            for (uint32_t i = 0; i < numFrames; ++i) {
                mix[i] += gain_ * v.nextSample(timer_);
            }
        }
    }

private:
    void triggerRow(uint32_t row) {
        for (uint32_t c = 0; c < numColumns_; ++c) {
            const Cell& cell = pattern_->cells[row][c];
            if (cell.isNote()) {
                const float vol = cell.volume == Cell::kVolumeDefault ? 1.0f : cell.volume / 127.0f;
                voices_[c].trigger(midiNoteToHz(cell.note), vol);
            } else if (cell.isOff()) {
                voices_[c].release();
            }
        }
    }

    void releaseAll() {
        for (auto& v : voices_) v.release();
    }

    std::array<synth::WavetableSynth, config::kMaxColumns> voices_;
    Timer timer_;
    const Pattern* pattern_ = nullptr;
    uint64_t startTick_ = 0;
    uint32_t numColumns_ = 1;
    float gain_ = 1.0f;
};

} // namespace adaptive
