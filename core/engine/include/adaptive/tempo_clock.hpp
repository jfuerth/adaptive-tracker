#pragma once

#include <adaptive/config.hpp>

#include <cstdint>

namespace adaptive {

/**
 * Sample-accurate tick clock with no cumulative drift.
 *
 * Time is kept as an exact rational: each sample adds (tempo in milli-BPM x
 * ticks per beat) to an accumulator, and a tick elapses each time it reaches
 * (sample rate x 60000). Integer-only, so results are identical on desktop and
 * MCU and never drift, however long the song runs.
 */
class TempoClock {
public:
    explicit TempoClock(uint32_t sampleRate, uint32_t tempoMilliBpm = 120000)
        : threshold_(static_cast<uint64_t>(sampleRate) * 60000u) {
        setTempoMilliBpm(tempoMilliBpm);
    }

    /// Tempo in thousandths of a BPM (120 BPM = 120000). Takes effect from the
    /// next sample; the phase within the current tick is kept.
    void setTempoMilliBpm(uint32_t milliBpm) {
        if (milliBpm == 0) milliBpm = 1;
        increment_ = static_cast<uint64_t>(milliBpm) * config::kTicksPerBeat;
        // A tick must last at least one sample, so advance() crosses at most one tick.
        if (increment_ > threshold_) increment_ = threshold_;
    }

    /// Index of the current tick (the tick that the next sample belongs to).
    uint64_t tick() const { return tick_; }

    /// Samples until the next tick boundary. Always >= 1.
    uint32_t samplesUntilNextTick() const {
        uint64_t remaining = threshold_ - accumulator_;
        return static_cast<uint32_t>((remaining + increment_ - 1) / increment_);
    }

    /**
     * Advance by `samples`, which must not exceed samplesUntilNextTick().
     * @return true if a tick boundary was reached; tick() is then the new tick.
     */
    bool advance(uint32_t samples) {
        accumulator_ += increment_ * samples;
        if (accumulator_ >= threshold_) {
            accumulator_ -= threshold_;
            ++tick_;
            return true;
        }
        return false;
    }

    void reset() {
        tick_ = 0;
        accumulator_ = 0;
    }

private:
    uint64_t threshold_;
    uint64_t increment_ = 0;
    uint64_t accumulator_ = 0;
    uint64_t tick_ = 0;
};

} // namespace adaptive
