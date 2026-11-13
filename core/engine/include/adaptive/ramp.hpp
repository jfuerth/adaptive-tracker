#pragma once

#include <cstdint>

namespace adaptive {

/**
 * Linear glide from the current value to a target over a number of steps
 * (samples, or blocks of samples). Used for click-free gain changes and for
 * smoothly moving filter cutoffs and tempo.
 */
class Ramp {
public:
    explicit Ramp(float value = 0.0f) : current_(value), target_(value) {}

    /// Glide to `target` over `steps` steps (0 = jump immediately).
    void set(float target, uint32_t steps) {
        target_ = target;
        if (steps == 0) {
            current_ = target;
            remaining_ = 0;
            return;
        }
        remaining_ = steps;
        step_ = (target_ - current_) / static_cast<float>(steps);
    }

    /// Jump to `value` with no glide.
    void reset(float value) { set(value, 0); }

    /// Advance one step and return the new value.
    float next() {
        if (remaining_ == 0) return current_;
        if (--remaining_ == 0) {
            current_ = target_;
        } else {
            current_ += step_;
        }
        return current_;
    }

    /// Advance `steps` steps at once.
    void advance(uint32_t steps) {
        if (steps >= remaining_) {
            current_ = target_;
            remaining_ = 0;
        } else {
            remaining_ -= steps;
            current_ += step_ * static_cast<float>(steps);
        }
    }

    float value() const { return current_; }
    float target() const { return target_; }
    bool isMoving() const { return remaining_ != 0; }

private:
    float current_;
    float target_;
    float step_ = 0.0f;
    uint32_t remaining_ = 0;
};

} // namespace adaptive
