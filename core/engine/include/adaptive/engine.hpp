#pragma once

#include <adaptive/channel.hpp>
#include <adaptive/config.hpp>
#include <adaptive/pitch.hpp>
#include <adaptive/song.hpp>
#include <adaptive/spsc_queue.hpp>
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
 * Top-level renderer: tick clock, channels, master output stage, and the
 * adaptive director (sections, transitions, layers, events).
 *
 * All memory is allocated at construction. render() never allocates, throws or
 * blocks, so it can be called from an audio callback.
 *
 * Threading: post() may be called from one other thread (the UI or control
 * thread). Everything else must be called on the audio thread, or before audio
 * starts.
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

    /// The adaptive structure to play. Must outlive its use. Call before posting
    /// commands that refer to sections, layers or events.
    void setSong(const Song* song) { song_ = song; }

    /**
     * Send a command to the audio thread. Safe to call from one producer thread
     * while audio runs. Returns false if the queue is full (the command is dropped).
     */
    bool post(const Command& command) { return queue_.push(command); }

    /// Index of the section playing now, or kNoIndex.
    uint8_t currentSection() const { return currentSection_; }
    /// Tick at which the current section's first bar began.
    uint64_t sectionStartTick() const { return sectionStartTick_; }
    /// Commands dropped because the pending list was full.
    uint32_t droppedCommands() const { return droppedCommands_; }

    /// The earliest tick at which newly scheduled playback can start: the
    /// current tick if its processing has not happened yet, else the next one.
    uint64_t nextTick() const { return tickPending_ ? clock_.tick() : clock_.tick() + 1; }

    /// Render `numFrames` mono samples into `out` (overwrites it).
    void render(float* out, uint32_t numFrames) {
        for (uint32_t i = 0; i < numFrames; ++i) out[i] = 0.0f;

        uint32_t done = 0;
        while (done < numFrames) {
            if (tickPending_) {
                processTick(clock_.tick());
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
    struct Pending {
        Command command;
        uint64_t dueTick;
    };

    void processTick(uint64_t tick) {
        // Commands first, so a section that starts on this tick plays this tick's rows.
        Command c;
        while (queue_.pop(c)) schedule(c, tick);
        runDue(tick);
        for (auto& ch : channels_) ch.onTick(tick, pitch_);
    }

    /// First tick at or after `tick` that is on the requested boundary.
    uint64_t quantize(Quantize when, uint64_t tick) const {
        if (when == Quantize::Now || currentSection_ == kNoIndex) return tick;  // no bar grid yet
        uint64_t unit = config::kTicksPerBeat;
        if (when == Quantize::Bar) unit *= currentBeatsPerBar();
        const uint64_t base = sectionStartTick_ <= tick ? sectionStartTick_ : tick;
        return base + ((tick - base + unit - 1) / unit) * unit;
    }

    uint32_t currentBeatsPerBar() const {
        if (song_ != nullptr && currentSection_ < song_->numSections) {
            const uint8_t b = song_->sections[currentSection_].beatsPerBar;
            return b > 0 ? b : 4;
        }
        return 4;
    }

    void schedule(const Command& c, uint64_t tick) {
        if (c.kind == Command::Kind::FireEvent) {
            if (song_ == nullptr || c.a >= song_->numEvents) return;
            const GameEvent& e = song_->events[c.a];
            for (uint8_t i = 0; i < e.numActions; ++i) schedule(e.actions[i], tick);
            return;
        }
        if (c.kind == Command::Kind::GotoSection || c.kind == Command::Kind::Stop) {
            cancelPending(Command::Kind::GotoSection);  // the latest request wins
        }
        const uint64_t due = quantize(c.when, tick);
        if (due == tick) {
            execute(c, tick);
        } else {
            addPending(c, due);
        }
    }

    void addPending(const Command& c, uint64_t due) {
        if (numPending_ >= config::kMaxPendingCommands) {
            ++droppedCommands_;
            return;
        }
        pending_[numPending_++] = Pending{c, due};
    }

    void cancelPending(Command::Kind kind) {
        uint32_t keep = 0;
        for (uint32_t i = 0; i < numPending_; ++i) {
            if (pending_[i].command.kind != kind) pending_[keep++] = pending_[i];
        }
        numPending_ = keep;
    }

    /// Execute due commands in the order they were scheduled.
    void runDue(uint64_t tick) {
        uint32_t keep = 0;
        const uint32_t n = numPending_;
        for (uint32_t i = 0; i < n; ++i) {
            if (pending_[i].dueTick <= tick) {
                const Command c = pending_[i].command;
                execute(c, tick);  // may append (bridges), never beyond n
            } else {
                pending_[keep++] = pending_[i];
            }
        }
        // Keep anything appended during execution.
        for (uint32_t i = n; i < numPending_; ++i) pending_[keep++] = pending_[i];
        numPending_ = keep;
    }

    void execute(const Command& c, uint64_t tick) {
        switch (c.kind) {
        case Command::Kind::GotoSection:
            enterSection(c.a, c.entry, c.b, tick);
            break;
        case Command::Kind::PlayStinger:
            if (c.a < config::kMaxChannels && c.pattern != nullptr) channels_[c.a].playOnce(c.pattern, tick);
            break;
        case Command::Kind::SetLayerMute:
            if (song_ != nullptr && c.a < song_->numLayers) {
                const uint32_t mask = song_->layers[c.a];
                for (uint32_t ch = 0; ch < config::kMaxChannels; ++ch) {
                    if (mask & (1u << ch)) channels_[ch].setMuted(c.b != 0);
                }
            }
            break;
        case Command::Kind::Stop:
            for (auto& ch : channels_) ch.play(nullptr, 0);
            currentSection_ = kNoIndex;
            break;
        case Command::Kind::FireEvent:  // expanded in schedule()
        case Command::Kind::None:
            break;
        }
    }

    void enterSection(uint8_t index, Entry entry, uint8_t bridge, uint64_t tick) {
        if (song_ == nullptr || index >= song_->numSections) return;
        if (bridge < song_->numSections && song_->sections[bridge].bridgeBeats > 0) {
            startSection(bridge, Entry::Restart, tick);  // bridges always play from the top
            const uint64_t due = tick + uint64_t(song_->sections[bridge].bridgeBeats) * config::kTicksPerBeat;
            addPending(Command::gotoSection(index, Quantize::Now, Entry::Restart), due);
            return;
        }
        startSection(index, entry, tick);
    }

    void startSection(uint8_t index, Entry entry, uint64_t tick) {
        const Section& s = song_->sections[index];
        const bool keepPhase = entry == Entry::KeepPhase && currentSection_ != kNoIndex;
        const uint64_t start = keepPhase ? sectionStartTick_ : tick;
        for (uint32_t c = 0; c < config::kMaxChannels; ++c) {
            Channel& ch = channels_[c];
            if (s.sequences[c] != nullptr) {
                ch.playSequence(s.sequences[c], start);
            } else if (!ch.isOneShot()) {
                ch.play(nullptr, 0);  // not in this section; leave stingers alone
            }
        }
        if (s.tempoMilliBpm != 0) clock_.setTempoMilliBpm(s.tempoMilliBpm);
        currentSection_ = index;
        sectionStartTick_ = start;
    }

    uint32_t sampleRate_;
    TempoClock clock_;
    std::array<Channel, config::kMaxChannels> channels_;
    synth::OutputProcessor master_;
    PitchContext pitch_;
    bool tickPending_ = true;  // tick 0 starts at the first sample

    const Song* song_ = nullptr;
    SpscQueue<Command, config::kCommandQueueSize> queue_;
    Pending pending_[config::kMaxPendingCommands] = {};
    uint32_t numPending_ = 0;
    uint32_t droppedCommands_ = 0;
    uint8_t currentSection_ = kNoIndex;
    uint64_t sectionStartTick_ = 0;
};

} // namespace adaptive
