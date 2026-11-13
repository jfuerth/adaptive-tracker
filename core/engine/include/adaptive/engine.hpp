#pragma once

#include <adaptive/channel.hpp>
#include <adaptive/config.hpp>
#include <adaptive/pitch.hpp>
#include <adaptive/tempo_clock.hpp>

#include <output_processor.hpp>

#include <array>
#include <cstdint>
#include <utility>

namespace adaptive {

namespace detail {
template<std::size_t... I>
std::array<Channel, sizeof...(I)> makeChannels(float sampleRate, std::index_sequence<I...>) {
    return {{(static_cast<void>(I), Channel(sampleRate))...}};
}
} // namespace detail

/**
 * Top-level renderer: tick clock, channels, master output stage.
 *
 * All memory is allocated at construction. render() never allocates, throws or
 * blocks, so it can be called from an audio callback.
 */
class Engine {
public:
    explicit Engine(uint32_t sampleRate)
        : sampleRate_(sampleRate),
          clock_(sampleRate),
          channels_(detail::makeChannels(static_cast<float>(sampleRate),
                                         std::make_index_sequence<config::kMaxChannels>{})),
          master_(0.5f, static_cast<float>(sampleRate)) {}

    uint32_t sampleRate() const { return sampleRate_; }

    TempoClock& clock() { return clock_; }
    const TempoClock& clock() const { return clock_; }

    Channel& channel(uint32_t index) { return channels_[index]; }

    /// Master output stage: drive, clipping and post-filter from pressense.
    synth::OutputProcessor& master() { return master_; }

    /// Key, transpose and degree mapping used to resolve notes as they start.
    /// Notes already sounding keep their pitch.
    PitchContext& pitch() { return pitch_; }
    const PitchContext& pitch() const { return pitch_; }

    /// The earliest tick at which newly scheduled playback can start: the
    /// current tick if its processing has not happened yet, else the next one.
    uint64_t nextTick() const { return tickPending_ ? clock_.tick() : clock_.tick() + 1; }

    /// Render `numFrames` mono samples into `out` (overwrites it).
    void render(float* out, uint32_t numFrames) {
        for (uint32_t i = 0; i < numFrames; ++i) out[i] = 0.0f;

        uint32_t done = 0;
        while (done < numFrames) {
            if (tickPending_) {
                const uint64_t tick = clock_.tick();
                for (auto& ch : channels_) ch.onTick(tick, pitch_);
                tickPending_ = false;
            }
            uint32_t chunk = clock_.samplesUntilNextTick();
            if (chunk > numFrames - done) chunk = numFrames - done;
            for (auto& ch : channels_) ch.render(out + done, chunk);
            tickPending_ = clock_.advance(chunk);
            done += chunk;
        }

        master_.processBuffer(out, numFrames);
    }

private:
    uint32_t sampleRate_;
    TempoClock clock_;
    std::array<Channel, config::kMaxChannels> channels_;
    synth::OutputProcessor master_;
    PitchContext pitch_;
    bool tickPending_ = true;  // tick 0 starts at the first sample
};

} // namespace adaptive
