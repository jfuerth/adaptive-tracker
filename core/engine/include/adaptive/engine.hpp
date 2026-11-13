#pragma once

#include <adaptive/channel.hpp>
#include <adaptive/config.hpp>
#include <adaptive/pitch.hpp>
#include <adaptive/ramp.hpp>
#include <adaptive/song.hpp>
#include <adaptive/spsc_queue.hpp>
#include <adaptive/tempo_clock.hpp>

#include <output_processor.hpp>

#include <array>
#include <cmath>
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
          master_(0.5f, static_cast<float>(sampleRate)),
          masterCutoffLog2_(std::log2(10000.0f)),  // pressense's post-filter default
          previews_(detail::makeVoices(static_cast<float>(sampleRate),
                                       std::make_index_sequence<config::kPreviewVoices>{})) {
        for (float& g : layerGains_) g = 1.0f;
    }

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

    /// Base tempo; the TempoScale mapping (if any) multiplies it. Sections with
    /// a tempo set it too. Use this rather than setTempoMilliBpm().
    void setTempoMilliBpm(uint32_t milliBpm) {
        baseTempoMilliBpm_ = milliBpm;
        applyTempo();
    }

    /**
     * The adaptive structure to play. Must outlive its use. Applies every
     * mapping once with the current parameter values. Call before audio starts,
     * or on the audio thread.
     */
    void setSong(const Song* song) {
        song_ = song;
        if (song_ == nullptr) return;
        for (uint8_t i = 0; i < song_->numMappings; ++i) {
            // Starting values apply instantly; only later changes glide.
            applyMapping(song_->mappings[i], params_[song_->mappings[i].param], 0, false, 0.0f, true);
        }
    }

    /// Current value of a parameter (audio thread).
    float param(uint8_t index) const { return index < config::kMaxParams ? params_[index] : 0.0f; }

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

        // Glides that move once per audio buffer.
        if (tempoScale_.isMoving()) {
            tempoScale_.advance(numFrames);
            applyTempo();
        }
        if (masterCutoffLog2_.isMoving()) {
            masterCutoffLog2_.advance(numFrames);
            master_.getPostFilter().setCutoff(std::exp2(masterCutoffLog2_.value()));
        }

        uint32_t done = 0;
        while (done < numFrames) {
            if (tickPending_) {
                processTick(clock_.tick());
                tickPending_ = false;
            }
            uint32_t chunk = clock_.samplesUntilNextTick();
            if (chunk > numFrames - done) chunk = numFrames - done;
            for (auto& ch : channels_) ch.render(out + done, chunk);
            renderPreviews(out + done, chunk);
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

        // Rows can fire events (Bxx). An event that restarts channels on this
        // tick gets those channels' rows played too: channels only process a
        // tick again if they were restarted. Bounded, in case events chain.
        for (int pass = 0; pass < 4; ++pass) {
            for (auto& ch : channels_) ch.onTick(tick, pitch_);
            uint8_t events[config::kMaxChannels * config::kMaxColumns];
            uint32_t n = 0;
            for (auto& ch : channels_) {
                for (uint32_t i = 0; i < ch.numFiredEvents(); ++i) events[n++] = ch.firedEvent(i);
                ch.clearFiredEvents();
            }
            if (n == 0) break;
            for (uint32_t i = 0; i < n; ++i) schedule(Command::fireEvent(events[i]), tick);
        }
    }

    void renderPreviews(float* out, uint32_t n) {
        for (auto& v : previews_) {
            if (!v.isActive()) continue;
            for (uint32_t i = 0; i < n; ++i) out[i] += v.nextSample(previewTimer_);
        }
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
            const Event& e = song_->events[c.a];
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
        case Command::Kind::SetParam:
            setParam(c.a, c.value, tick);
            break;
        case Command::Kind::SetKey:
            pitch_.current = Key{static_cast<uint8_t>(c.a % 12), c.data};
            break;
        case Command::Kind::SetTempo:
            if (c.value > 0.0f) setTempoMilliBpm(static_cast<uint32_t>(c.value * 1000.0f));
            break;
        case Command::Kind::SetCell:
            if (c.target != nullptr && c.data < config::kMaxRows && c.a < config::kMaxColumns) {
                c.target->cells[c.data][c.a] = c.cell;
            }
            break;
        case Command::Kind::PreviewOn:
            if (c.a < config::kMaxChannels && c.b < config::kPreviewVoices) {
                synth::WavetableSynth& v = previews_[c.b];
                v = channels_[c.a].voice(0);  // borrow the channel's patch
                v.trigger(midiNoteToHz(static_cast<float>(c.data & 0x7F)), c.value);
            }
            break;
        case Command::Kind::PreviewOff:
            if (c.b < config::kPreviewVoices) previews_[c.b].release();
            break;
        case Command::Kind::FireEvent:  // expanded in schedule()
        case Command::Kind::None:
            break;
        }
    }

    uint32_t msToSamples(uint16_t ms) const { return static_cast<uint32_t>(uint64_t(ms) * sampleRate_ / 1000); }

    void setParam(uint8_t index, float value, uint64_t tick) {
        if (index >= config::kMaxParams) return;
        if (value < 0.0f) value = 0.0f;
        if (value > 1.0f) value = 1.0f;
        const float old = params_[index];
        params_[index] = value;
        if (song_ == nullptr) return;
        for (uint8_t i = 0; i < song_->numMappings; ++i) {
            if (song_->mappings[i].param == index) applyMapping(song_->mappings[i], value, tick, true, old);
        }
    }

    /// Drive a mapping's target from parameter value `p`. Threshold mappings
    /// fire only when `fireEvents` is set and `p` crosses from `old`.
    void applyMapping(const Mapping& m, float p, uint64_t tick, bool fireEvents, float old = 0.0f,
                      bool immediate = false) {
        const uint32_t ramp = immediate ? 0 : msToSamples(m.rampMs);
        const float out = m.map(p);
        switch (m.target) {
        case Mapping::Target::ChannelGain:
            if (m.index < config::kMaxChannels) channels_[m.index].setGain(out, ramp);
            break;
        case Mapping::Target::LayerGain:
            if (m.index < config::kMaxLayers) {
                layerGains_[m.index] = out;
                updateLayerGains(ramp);
            }
            break;
        case Mapping::Target::ChannelCutoff:
            if (m.index < config::kMaxChannels) channels_[m.index].setCutoff(out, ramp);
            break;
        case Mapping::Target::MasterCutoff:
            masterCutoffLog2_.set(std::log2(out < 20.0f ? 20.0f : out), ramp);
            if (ramp == 0) master_.getPostFilter().setCutoff(out);
            break;
        case Mapping::Target::TempoScale:
            tempoScale_.set(out > 0.05f ? out : 0.05f, ramp);
            if (ramp == 0) applyTempo();
            break;
        case Mapping::Target::Transpose:
            pitch_.transpose = static_cast<int8_t>(std::lround(out));
            break;
        case Mapping::Target::Threshold:
            if (!fireEvents) break;
            if (old < m.threshold && p >= m.threshold) {
                schedule(Command::fireEvent(m.index), tick);
            } else if (old >= m.threshold && p < m.threshold && m.indexBelow != kNoIndex) {
                schedule(Command::fireEvent(m.indexBelow), tick);
            }
            break;
        case Mapping::Target::None:
            break;
        }
    }

    /// Each channel's layer gain is the product of the gains of the layers it is in.
    void updateLayerGains(uint32_t ramp) {
        if (song_ == nullptr) return;
        for (uint32_t c = 0; c < config::kMaxChannels; ++c) {
            float g = 1.0f;
            for (uint8_t l = 0; l < song_->numLayers; ++l) {
                if (song_->layers[l] & (1u << c)) g *= layerGains_[l];
            }
            channels_[c].setLayerGain(g, ramp);
        }
    }

    void applyTempo() {
        const float t = static_cast<float>(baseTempoMilliBpm_) * tempoScale_.value();
        clock_.setTempoMilliBpm(static_cast<uint32_t>(t));
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
        if (s.tempoMilliBpm != 0) setTempoMilliBpm(s.tempoMilliBpm);
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

    // Parameters and mapping targets.
    float params_[config::kMaxParams] = {};
    float layerGains_[config::kMaxLayers] = {};  // set to 1 in the constructor
    uint32_t baseTempoMilliBpm_ = 120000;
    Ramp tempoScale_{1.0f};
    Ramp masterCutoffLog2_;

    // Editor note previews.
    std::array<synth::WavetableSynth, config::kPreviewVoices> previews_;
    Channel::Timer previewTimer_;
};

} // namespace adaptive
