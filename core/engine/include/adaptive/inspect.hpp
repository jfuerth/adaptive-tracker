#pragma once

#include <adaptive/instrument.hpp>

#include <adsr_envelope.hpp>
#include <wavetable_oscillator.hpp>

#include <cstdint>

namespace adaptive::inspect {

/**
 * Helpers for editors: run pressense's own oscillator and envelopes so the UI
 * can draw what the synth actually produces, rather than a re-implementation.
 * Not for the audio thread.
 */

/// `n` samples of the oscillator at `shape`, spanning `cycles` cycles from phase 0.
inline void renderWaveform(float shape, float cycles, float* out, uint32_t n) {
    constexpr float kRate = 48000.0f;
    synth::WavetableOscillator osc(kRate);
    osc.updateWavetable(shape);
    osc.reset();
    const float frequency = kRate * cycles / static_cast<float>(n);
    for (uint32_t i = 0; i < n; ++i) out[i] = osc.nextSample(frequency);
}

/**
 * An envelope's response to a note held until it reaches sustain, then
 * released. Each moving stage (attack, decay, release) is described by how
 * long it really took and `kPoints` levels evenly spaced through it, so an
 * editor can draw each stage at its own scale.
 */
struct EnvelopeTrace {
    static constexpr uint32_t kPoints = 32;
    static constexpr uint32_t kStages = 3;  ///< attack, decay, release
    float seconds[kStages] = {};            ///< how long each stage took
    float levels[kStages][kPoints] = {};
    float sustain = 0.0f;
    /// True if the release didn't reach silence within the time limit.
    bool releaseUnfinished = false;
};

inline EnvelopeTrace traceEnvelope(float attack, float decay, float sustain, float release) {
    constexpr float kRate = 8000.0f;        // 0.125 ms resolution
    constexpr uint32_t kLimit = 30 * 8000;  // 30 s per stage

    // The envelope is deterministic, so run it twice: once to measure each
    // stage, then again to sample evenly spaced points through each stage.
    // Visits every sample: visit(stage, indexInStage, level).
    auto run = [&](auto&& visit) {
        using Phase = synth::AdsrEnvelope::Phase;
        synth::AdsrEnvelope env(kRate);
        env.setParameters(attack, decay, sustain, release);
        env.trigger();
        uint32_t n = 0;
        for (n = 0; n < kLimit && env.getPhase() == Phase::ATTACK; ++n) visit(0, n, env.nextSample());
        for (n = 0; n < kLimit && env.getPhase() == Phase::DECAY; ++n) visit(1, n, env.nextSample());
        env.release();
        for (n = 0; n < kLimit && env.isActive(); ++n) visit(2, n, env.nextSample());
        return env.isActive();
    };

    uint32_t length[EnvelopeTrace::kStages] = {};
    run([&](uint32_t stage, uint32_t, float) { ++length[stage]; });

    EnvelopeTrace t;
    t.sustain = sustain;
    for (uint32_t s = 0; s < EnvelopeTrace::kStages; ++s) t.seconds[s] = static_cast<float>(length[s]) / kRate;
    // Point 0 of each stage is where it starts (the previous stage's end);
    // point kPoints-1 is its last sample.
    float last = 0.0f;
    uint32_t next[EnvelopeTrace::kStages] = {};
    t.releaseUnfinished = run([&](uint32_t stage, uint32_t i, float level) {
        const uint32_t len = length[stage];
        while (next[stage] < EnvelopeTrace::kPoints) {
            const uint32_t k = next[stage];
            // Sample index for point k: point 0 is "before the first sample".
            const uint64_t want = (static_cast<uint64_t>(k) * len) / (EnvelopeTrace::kPoints - 1);
            if (want == 0) {
                t.levels[stage][next[stage]++] = last;
            } else if (want == i + 1) {
                t.levels[stage][next[stage]++] = level;
            } else {
                break;
            }
        }
        last = level;
    });
    // A stage that took no samples stays flat at its starting level.
    for (uint32_t s = 0; s < EnvelopeTrace::kStages; ++s) {
        for (uint32_t k = next[s]; k < EnvelopeTrace::kPoints; ++k) {
            t.levels[s][k] = s == 0 ? 0.0f : t.levels[s - 1][EnvelopeTrace::kPoints - 1];
        }
    }
    return t;
}

} // namespace adaptive::inspect
