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
#include <atomic>
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

/// A changed copy of the song for the engine to play from now on, and how
/// the change renumbered things (see Engine::offer).
struct SongSwap {
    Song song;
    Renumbering renumbering;
};

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
        // Seed every voice's noise from its position rather than leaving it to
        // the order voices happen to be built in, so a song renders the same
        // every time, in any engine.
        uint32_t seed = 0;
        for (Channel& ch : channels_) {
            for (uint32_t col = 0; col < config::kMaxColumns; ++col) ch.voice(col).setNoiseSeed(++seed);
        }
        for (synth::WavetableSynth& v : previews_) v.setNoiseSeed(++seed);
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
     * or on the audio thread. While it plays, the song is changed only through
     * commands (instrument edits and assignments), on the audio thread.
     */
    void setSong(Song* song) {
        song_ = song;
        if (song_ == nullptr) return;
        // Channels the song sets up; others keep whatever setup they have.
        for (uint32_t ch = 0; ch < song_->numChannels; ++ch) setUpChannel(ch);
        for (uint32_t ch = 0; ch < config::kMaxChannels; ++ch) {
            for (uint32_t col = 0; col < config::kMaxColumns; ++col) {
                const uint8_t i = song_->columnInstrument(ch, col);
                if (i < song_->numInstruments) song_->instruments[i].applyTo(channels_[ch].voice(col));
            }
        }
        for (uint32_t i = 0; i < config::kMaxParams; ++i) {
            params_[i] = i < song_->numParams ? song_->paramInitial[i] : 0.0f;
        }
        pitch_.home = song_->homeKey;
        pitch_.current = song_->homeKey;
        setTempoMilliBpm(song_->tempoMilliBpm);
        for (uint8_t i = 0; i < song_->numMappings; ++i) {
            // Starting values apply instantly; only later changes glide.
            applyMapping(song_->mappings[i], params_[song_->mappings[i].param], 0, false, 0.0f, true);
        }
        for (auto& m : layerMuted_) m = false;
        updateMutes();
        updateGates();
        updateMix();
    }

    /// Current value of a parameter (audio thread).
    float param(uint8_t index) const { return index < config::kMaxParams ? params_[index] : 0.0f; }

    /**
     * Send a command to the audio thread. Safe to call from one producer thread
     * while audio runs. Returns false if the queue is full (the command is dropped).
     */
    bool post(const Command& command) { return queue_.push(command); }

    /**
     * Hand the engine a whole new song (the current one, changed), from the
     * posting thread. Playback carries on in place, following
     * `swap->renumbering`. The swap must stay alive while the engine uses its
     * song; once swapsTaken() has passed it, the song it replaced is no
     * longer used. Returns false (nothing changes) if the swap couldn't be
     * queued.
     */
    bool offer(SongSwap* swap) {
        for (uint8_t slot = 0; slot < kSwapSlots; ++slot) {
            if (swapSlots_[slot].load(std::memory_order_acquire) != nullptr) continue;
            swapSlots_[slot].store(swap, std::memory_order_release);
            if (post(Command::swapSong(slot))) return true;
            swapSlots_[slot].store(nullptr, std::memory_order_release);
            return false;
        }
        return false;
    }
    /// How many offered swaps the audio thread has taken, in the order offered. Any thread.
    uint32_t swapsTaken() const { return swapsTaken_.load(std::memory_order_acquire); }

    /// True if any channel is playing a sequence or pattern (stingers excluded).
    bool isPlaying() const {
        for (const auto& ch : channels_) {
            if (ch.isPlaying() && !ch.isOneShot()) return true;
        }
        return false;
    }

    /// The song playing (audio thread: it changes when a swap is taken).
    const Song* song() const { return song_; }

    /// Index of the section playing now, or kNoIndex.
    uint8_t currentSection() const { return currentSection_; }
    /// Tick at which the current section's first bar began.
    uint64_t sectionStartTick() const { return sectionStartTick_; }
    /// Commands dropped because the pending list was full.
    uint32_t droppedCommands() const { return droppedCommands_; }

    /// The earliest tick at which newly scheduled playback can start: the
    /// current tick if its processing has not happened yet, else the next one.
    uint64_t nextTick() const { return tickPending_ ? clock_.tick() : clock_.tick() + 1; }

    /// Instrument heard on the monitor output, or kNoIndex.
    uint8_t monitoredInstrument() const { return monitor_; }

    /**
     * Render `numFrames` mono samples into `out` (overwrites it). If `monitor`
     * is set, it gets the monitored instrument's voices alone (see
     * Command::setMonitor), before channel filters, gains and the master stage;
     * silence when nothing is monitored.
     */
    void render(float* out, uint32_t numFrames, float* monitor = nullptr) {
        for (uint32_t i = 0; i < numFrames; ++i) out[i] = 0.0f;
        if (monitor != nullptr) {
            for (uint32_t i = 0; i < numFrames; ++i) monitor[i] = 0.0f;
        }

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
            // While the speed glides, pitch and tempo follow it in small steps.
            const bool gliding = speed_.isMoving();
            if (gliding && chunk > kSpeedStep) chunk = kSpeedStep;
            float* tap = monitor != nullptr ? monitor + done : nullptr;
            for (uint32_t c = 0; c < config::kMaxChannels; ++c) {
                channels_[c].render(out + done, chunk, tap, tap != nullptr ? monitoredColumns(c) : 0);
            }
            renderPreviews(out + done, chunk, tap);
            tickPending_ = clock_.advance(chunk);
            done += chunk;
            if (gliding) {
                speed_.advance(chunk);
                applySpeed();
            }
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

    void renderPreviews(float* out, uint32_t n, float* tap) {
        for (uint32_t s = 0; s < config::kPreviewVoices; ++s) {
            synth::WavetableSynth& v = previews_[s];
            if (!v.isActive()) continue;
            if (tap != nullptr && monitor_ != kNoIndex && previewInstrument_[s] == monitor_) {
                for (uint32_t i = 0; i < n; ++i) {
                    const float x = v.nextSample(previewTimer_);
                    out[i] += x;
                    tap[i] += x;
                }
            } else {
                for (uint32_t i = 0; i < n; ++i) out[i] += v.nextSample(previewTimer_);
            }
        }
    }

    /// Bitmask of channel `c`'s columns that play the monitored instrument.
    uint32_t monitoredColumns(uint32_t c) const {
        if (song_ == nullptr || monitor_ == kNoIndex) return 0;
        uint32_t mask = 0;
        for (uint32_t col = 0; col < config::kMaxColumns; ++col) {
            if (song_->columnInstrument(c, col) == monitor_) mask |= 1u << col;
        }
        return mask;
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
            if (c.a < config::kMaxChannels && song_ != nullptr && song_->pattern(c.data) != nullptr) {
                channels_[c.a].playOnce(song_->pattern(c.data), tick);
            }
            break;
        case Command::Kind::SetLayerMute:
            if (song_ != nullptr && c.a < song_->numLayers) {
                layerMuted_[c.a] = c.b != 0;
                updateMutes();
            }
            break;
        case Command::Kind::Stop:
            for (auto& ch : channels_) {
                ch.play(nullptr, 0);
                ch.setColumnMask(~0u);
            }
            currentSection_ = kNoIndex;
            updateGates();
            break;
        case Command::Kind::SoloPattern:
            if (c.a < config::kMaxChannels && song_ != nullptr && song_->pattern(c.b) != nullptr) {
                for (auto& ch : channels_) {
                    ch.play(nullptr, 0);
                    ch.setColumnMask(~0u);
                }
                currentSection_ = kNoIndex;
                sectionStartTick_ = tick;  // beats and bars count from here
                freshStart(c.a);
                channels_[c.a].play(song_->pattern(c.b), tick);
                channels_[c.a].setColumnMask(c.data);
                updateGates();
            }
            break;
        case Command::Kind::SwapSong:
            if (c.a < kSwapSlots) {
                SongSwap* swap = swapSlots_[c.a].exchange(nullptr, std::memory_order_acq_rel);
                if (swap != nullptr && song_ != nullptr) {
                    const Song* old = song_;
                    song_ = &swap->song;
                    rebase(*old, swap->renumbering, tick);
                }
                if (swap != nullptr) swapsTaken_.fetch_add(1, std::memory_order_acq_rel);
            }
            break;
        case Command::Kind::SetChannelVolume:
            if (song_ != nullptr && song_->setChannelVolume(c.a, c.value)) updateMix();
            break;
        case Command::Kind::SetChannelMute:
            if (c.a < config::kMaxChannels) {
                channels_[c.a].setStripMuted(c.b != 0);
                updateMix();
            }
            break;
        case Command::Kind::SetChannelSolo:
            if (c.a < config::kMaxChannels) {
                channels_[c.a].setSoloed(c.b != 0);
                updateMix();
            }
            break;
        case Command::Kind::SetChannelPause:
            if (c.a < config::kMaxChannels) channels_[c.a].setPaused(c.b != 0);
            break;
        case Command::Kind::RestartChannel:
            if (c.a < config::kMaxChannels) channels_[c.a].restartAt(tick);
            break;
        case Command::Kind::SetSpeed:
            setSpeed(c.value, msToSamples(c.data));
            break;
        case Command::Kind::Rewind:
            if (static_cast<RewindTo>(c.a) == RewindTo::Bar) {
                rewindToBar(tick);
            } else {
                for (auto& ch : channels_) ch.restartPattern(tick);
            }
            break;
        case Command::Kind::HearAll:
            hearAll_ = c.a != 0;
            updateGates();
            updateMutes();
            updateLayerGains(0);
            break;
        case Command::Kind::SetColumnMask:
            if (c.a < config::kMaxChannels) channels_[c.a].setColumnMask(c.data);
            break;
        case Command::Kind::SetParam:
            setParam(c.a, c.value, tick);
            updateGates();
            break;
        case Command::Kind::SetColumnPitch:
            if (song_ != nullptr && song_->setColumnPitch(c.a, c.b, static_cast<PitchMode>(c.data))) {
                channels_[c.a].setPitchMode(c.b, static_cast<PitchMode>(c.data));
            }
            break;
        case Command::Kind::SetColumnGate:
            if (song_ != nullptr && song_->setColumnGate(c.a, c.b, c.data, c.value)) updateGates();
            break;
        case Command::Kind::SetLayerColumns:
            if (song_ != nullptr && song_->setLayerColumns(c.a, c.b, c.data)) {
                updateMutes();
                updateLayerGains(0);
            }
            break;
        case Command::Kind::RemoveLayer:
            if (song_ != nullptr && song_->removeLayer(c.a)) {
                for (uint32_t l = c.a; l + 1 < config::kMaxLayers; ++l) {
                    layerMuted_[l] = layerMuted_[l + 1];
                    layerGains_[l] = layerGains_[l + 1];
                }
                layerMuted_[config::kMaxLayers - 1] = false;
                layerGains_[config::kMaxLayers - 1] = 1.0f;
                updateMutes();
                updateLayerGains(0);
            }
            break;
        case Command::Kind::SetKey:
            pitch_.current = Key{static_cast<uint8_t>(c.a % 12), c.data};
            break;
        case Command::Kind::SetTempo:
            if (c.value > 0.0f) setTempoMilliBpm(static_cast<uint32_t>(c.value * 1000.0f));
            break;
        case Command::Kind::SetCell:
            if (song_ != nullptr && c.b < song_->numPatterns && c.data < config::kMaxRows &&
                c.a < config::kMaxColumns) {
                song_->patterns[c.b].cells[c.data][c.a] = c.cell;
            }
            break;
        case Command::Kind::PreviewOn:
            if (song_ != nullptr && c.a < song_->numInstruments && c.b < config::kPreviewVoices) {
                synth::WavetableSynth& v = previews_[c.b];
                song_->instruments[c.a].applyTo(v);
                previewInstrument_[c.b] = c.a;
                v.trigger(midiNoteToHz(static_cast<float>(c.data & 0x7F)), c.value);
            }
            break;
        case Command::Kind::PreviewOff:
            if (c.b < config::kPreviewVoices) previews_[c.b].release();
            break;
        case Command::Kind::SetInstrumentParam:
            if (song_ != nullptr && c.a < song_->numInstruments && c.b < Instrument::kNumParams) {
                const auto p = static_cast<Instrument::Param>(c.b);
                Instrument& inst = song_->instruments[c.a];
                inst.set(p, c.value);
                applyInstrumentParam(c.a, inst, p);
            }
            break;
        case Command::Kind::AddInstrument:
            if (song_ != nullptr && c.a < config::kMaxInstruments && c.a >= song_->numInstruments) {
                song_->numInstruments = static_cast<uint8_t>(c.a + 1);
            }
            break;
        case Command::Kind::AssignInstrument:
            if (song_ != nullptr && c.a < config::kMaxChannels && c.b < config::kMaxColumns) {
                const uint8_t i = static_cast<uint8_t>(c.data);
                song_->channels[c.a].instruments[c.b] = i < song_->numInstruments ? i : kNoIndex;
                if (i < song_->numInstruments) song_->instruments[i].applyTo(channels_[c.a].voice(c.b));
            }
            break;
        case Command::Kind::SetMonitor:
            monitor_ = c.a;
            break;
        case Command::Kind::SetPatternShape:
            if (song_ != nullptr && song_->setPatternShape(c.b, c.data, c.a)) {
                for (auto& ch : channels_) ch.patternChanged(&song_->patterns[c.b], tick);
            }
            break;
        case Command::Kind::ClearCells:
            if (song_ != nullptr) song_->clearCells(c.b, c.a, c.data & 0xFFu, c.data >> 8, c.parts);
            break;
        case Command::Kind::Added:
            if (song_ != nullptr) added(static_cast<Addition>(c.a), c.b);
            break;
        case Command::Kind::EditSequence:
            if (song_ != nullptr && song_->editSequence(c.a, c.b, static_cast<SeqEdit>(c.parts), c.data & 0xFFu, c.data >> 8)) {
                sequenceEdited(c.a, c.b, static_cast<SeqEdit>(c.parts), c.data & 0xFFu, tick);
            }
            break;
        case Command::Kind::SetSection:
            if (song_ != nullptr &&
                song_->setSectionTiming(c.a, static_cast<uint32_t>(c.value * 1000.0f + 0.5f), c.b, c.data) &&
                c.a == currentSection_ && song_->sections[c.a].tempoMilliBpm != 0) {
                setTempoMilliBpm(song_->sections[c.a].tempoMilliBpm);
            }
            break;
        case Command::Kind::ShiftRows:
            if (song_ != nullptr) song_->shiftRows(c.b, c.a, c.data, c.parts != 0);
            break;
        case Command::Kind::ScaleRows:
            if (song_ != nullptr && song_->scaleRows(c.b, c.a, c.data)) {
                for (auto& ch : channels_) ch.rowsScaled(&song_->patterns[c.b], c.a, c.data, tick);
            }
            break;
        case Command::Kind::SetColumns:
            if (song_ != nullptr && c.a < song_->numChannels) {
                const uint32_t before = song_->channels[c.a].numColumns;
                if (song_->setColumns(c.a, c.b)) {
                    channels_[c.a].setNumColumns(c.b);
                    applyColumnInstruments(c.a, before);
                }
            }
            break;
        case Command::Kind::RemoveColumn:
            if (song_ != nullptr && song_->removeColumn(c.a, c.b)) {
                channels_[c.a].setNumColumns(song_->channels[c.a].numColumns);
                channels_[c.a].releaseFrom(c.b);  // their cells moved; start them afresh
                applyColumnInstruments(c.a, c.b);
            }
            break;
        case Command::Kind::FireEvent:  // expanded in schedule()
        case Command::Kind::None:
            break;
        }
    }

    /// Set the voices of `channel`'s columns from `from` onward to their instruments.
    void applyColumnInstruments(uint32_t channel, uint32_t from) {
        for (uint32_t col = from; col < config::kMaxColumns; ++col) {
            const uint8_t i = song_->columnInstrument(channel, col);
            if (i < song_->numInstruments) song_->instruments[i].applyTo(channels_[channel].voice(col));
        }
    }

    /**
     * A channel starting from the top sounds as written, whatever played
     * before: every voice back to its column's instrument (a column without
     * one gets the default sound), so filter, vibrato, wave shape and
     * resonance set by effects don't carry over, and the columns forget
     * their effect memories.
     */
    void freshStart(uint32_t channel) {
        channels_[channel].forgetEffects();
        for (uint32_t col = 0; col < config::kMaxColumns; ++col) {
            const uint8_t i = song_->columnInstrument(channel, col);
            (i < song_->numInstruments ? song_->instruments[i] : Instrument()).applyTo(channels_[channel].voice(col));
        }
    }

    /// Push an edited parameter to every voice using instrument `index`,
    /// including previews, so a held note changes as the control moves.
    void applyInstrumentParam(uint8_t index, const Instrument& inst, Instrument::Param p) {
        if (song_ != nullptr) {
            for (uint32_t ch = 0; ch < config::kMaxChannels; ++ch) {
                for (uint32_t col = 0; col < config::kMaxColumns; ++col) {
                    if (song_->columnInstrument(ch, col) == index) inst.applyParam(channels_[ch].voice(col), p);
                }
            }
        }
        for (uint32_t s = 0; s < config::kPreviewVoices; ++s) {
            if (previewInstrument_[s] == index) inst.applyParam(previews_[s], p);
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
        case Mapping::Target::Speed:
            setSpeed(out, ramp);
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
    /// Each column's layer gain is the product of the gains of the layers it is in.
    void updateLayerGains(uint32_t ramp) {
        if (song_ == nullptr) return;
        for (uint32_t ch = 0; ch < config::kMaxChannels; ++ch) {
            for (uint32_t col = 0; col < config::kMaxColumns; ++col) {
                float g = 1.0f;
                for (uint8_t l = 0; !hearAll_ && l < song_->numLayers; ++l) {
                    if (song_->layers[l] & layerBit(ch, col)) g *= layerGains_[l];
                }
                channels_[ch].setLayerGain(col, g, ramp);
            }
        }
    }

    /// A column is muted while any layer it is in is muted.
    void updateMutes() {
        uint32_t muted = 0;
        for (uint8_t l = 0; !hearAll_ && l < song_->numLayers; ++l) {
            if (layerMuted_[l]) muted |= song_->layers[l];
        }
        for (uint32_t ch = 0; ch < config::kMaxChannels; ++ch) {
            channels_[ch].setMutedColumns((muted >> (ch * config::kMaxColumns)) & ((1u << config::kMaxColumns) - 1u));
        }
    }

    /// Opens or closes column gates from the parameters (all open while
    /// hearing everything).
    void updateGates() {
        if (song_ == nullptr) return;
        for (uint32_t ch = 0; ch < config::kMaxChannels; ++ch) {
            uint32_t open = ~0u;
            if (!hearAll_ && ch < song_->numChannels) {
                for (uint32_t col = 0; col < config::kMaxColumns; ++col) {
                    if (!song_->gateOpen(ch, col, params_)) open &= ~(1u << col);
                }
            }
            channels_[ch].setOpenColumns(open);
        }
    }

    /// Faders, mutes and solos to each channel's mix gain.
    void updateMix() {
        bool anySolo = false;
        for (const auto& ch : channels_) anySolo = anySolo || ch.soloed();
        for (uint32_t c = 0; c < config::kMaxChannels; ++c) {
            Channel& ch = channels_[c];
            const float fader = song_ != nullptr && c < song_->numChannels ? song_->channels[c].volume : 1.0f;
            ch.setMix(fader, ch.stripMuted() || (anySolo && !ch.soloed()));
        }
    }

    void setUpChannel(uint32_t ch) {
        for (uint32_t col = 0; col < config::kMaxColumns; ++col) {
            channels_[ch].setPitchMode(col, song_->channels[ch].pitchModes[col]);
        }
        channels_[ch].setNumColumns(song_->channels[ch].numColumns);
    }

    void applyTempo() {
        const float t = static_cast<float>(baseTempoMilliBpm_) * tempoScale_.value() * speed_.value();
        clock_.setTempoMilliBpm(static_cast<uint32_t>(t));
    }

    /// Samples between pitch and tempo updates while the speed glides.
    static constexpr uint32_t kSpeedStep = 64;

    void setSpeed(float speed, uint32_t rampSamples) {
        speed_.set(speed < 0.05f ? 0.05f : speed > 4.0f ? 4.0f : speed, rampSamples);
        if (rampSamples == 0) applySpeed();
    }
    /// Tape speed: tempo and every channel's pitch.
    void applySpeed() {
        applyTempo();
        for (auto& ch : channels_) ch.setSpeed(speed_.value());
    }

    /// Where a channel was in the old song: a sequence (section, channel) or a single pattern.
    struct PlayedFrom {
        uint8_t section = kNoIndex;
        uint8_t channel = kNoIndex;
        uint8_t pattern = kNoIndex;
    };

    PlayedFrom playedFrom(const Channel& ch, const Song& old) const {
        PlayedFrom w;
        if (const Sequence* seq = ch.playingSequence()) {
            for (uint8_t sec = 0; sec < config::kMaxSections; ++sec) {
                const Sequence* first = old.sections[sec].sequences;
                if (seq >= first && seq < first + config::kMaxChannels) {
                    w.section = sec;
                    w.channel = static_cast<uint8_t>(seq - first);
                }
            }
        } else if (const Pattern* p = ch.singlePattern()) {
            if (p >= old.patterns && p < old.patterns + config::kMaxPatterns) {
                w.pattern = static_cast<uint8_t>(p - old.patterns);
            }
        }
        return w;
    }

    static uint8_t renumbered(const uint8_t* map, uint32_t size, uint8_t index) {
        return index < size ? map[index] : kNoIndex;
    }

    /// song_ was just replaced by a changed copy of `old`: carry on playing in place.
    void rebase(const Song& old, const Renumbering& r, uint64_t tick) {
        PlayedFrom was[config::kMaxChannels];
        for (uint32_t c = 0; c < config::kMaxChannels; ++c) was[c] = playedFrom(channels_[c], old);

        // Channels move with their numbers (the renumbering keeps their order).
        uint8_t from[config::kMaxChannels];
        for (auto& f : from) f = kNoIndex;
        for (uint32_t c = 0; c < config::kMaxChannels; ++c) {
            if (r.channels[c] < config::kMaxChannels) from[r.channels[c]] = static_cast<uint8_t>(c);
        }
        for (uint32_t c = 0; c < config::kMaxChannels; ++c) {
            if (from[c] != kNoIndex && from[c] > c) channels_[c] = std::move(channels_[from[c]]);
        }
        for (uint32_t c = config::kMaxChannels; c-- > 0;) {
            if (from[c] != kNoIndex && from[c] < c) channels_[c] = std::move(channels_[from[c]]);
        }

        currentSection_ = renumbered(r.sections, config::kMaxSections, currentSection_);
        if (currentSection_ >= song_->numSections) currentSection_ = kNoIndex;

        for (uint32_t c = 0; c < config::kMaxChannels; ++c) {
            Channel& ch = channels_[c];
            if (from[c] == kNoIndex) {
                ch.reset();
                joinSection(c);
                continue;
            }
            const PlayedFrom w = was[from[c]];
            const uint8_t sec = renumbered(r.sections, config::kMaxSections, w.section);
            const uint8_t seqCh = renumbered(r.channels, config::kMaxChannels, w.channel);
            const uint8_t pat = renumbered(r.patterns, config::kMaxPatterns, w.pattern);
            if (sec < song_->numSections && seqCh < config::kMaxChannels &&
                song_->isPlayable(song_->sections[sec].sequences[seqCh])) {
                ch.retarget(&song_->sections[sec].sequences[seqCh], song_->patterns, nullptr, tick);
            } else if (song_->pattern(pat) != nullptr) {
                ch.retarget(nullptr, nullptr, &song_->patterns[pat], tick);
            } else {
                ch.retarget(nullptr, nullptr, nullptr, tick);
                joinSection(c);
            }
        }

        bool muted[config::kMaxLayers] = {};
        float gains[config::kMaxLayers];
        for (auto& g : gains) g = 1.0f;
        for (uint32_t l = 0; l < config::kMaxLayers; ++l) {
            if (r.layers[l] < config::kMaxLayers) {
                muted[r.layers[l]] = layerMuted_[l];
                gains[r.layers[l]] = layerGains_[l];
            }
        }
        for (uint32_t l = 0; l < config::kMaxLayers; ++l) {
            layerMuted_[l] = muted[l];
            layerGains_[l] = gains[l];
        }

        // Scheduled commands name things by number too.
        uint32_t keep = 0;
        for (uint32_t i = 0; i < numPending_; ++i) {
            Command& pc = pending_[i].command;
            bool ok = true;
            switch (pc.kind) {
            case Command::Kind::GotoSection:
                pc.a = renumbered(r.sections, config::kMaxSections, pc.a);
                if (pc.b != kNoIndex) pc.b = renumbered(r.sections, config::kMaxSections, pc.b);
                ok = pc.a != kNoIndex;
                break;
            case Command::Kind::PlayStinger:
                pc.a = renumbered(r.channels, config::kMaxChannels, pc.a);
                pc.data = renumbered(r.patterns, config::kMaxPatterns, static_cast<uint8_t>(pc.data));
                ok = pc.a != kNoIndex && pc.data != kNoIndex;
                break;
            case Command::Kind::SetLayerMute:
                pc.a = renumbered(r.layers, config::kMaxLayers, pc.a);
                ok = pc.a != kNoIndex;
                break;
            case Command::Kind::SetChannelMute:
            case Command::Kind::SetChannelSolo:
            case Command::Kind::SetChannelPause:
            case Command::Kind::RestartChannel:
                pc.a = renumbered(r.channels, config::kMaxChannels, pc.a);
                ok = pc.a != kNoIndex;
                break;
            default:
                break;
            }
            if (ok) pending_[keep++] = pending_[i];
        }
        numPending_ = keep;

        // Everything else is read afresh from the new song.
        for (uint32_t ch = 0; ch < song_->numChannels; ++ch) {
            setUpChannel(ch);
            applyColumnInstruments(ch, 0);
        }
        pitch_.home = song_->homeKey;
        if (currentSection_ != kNoIndex && song_->sections[currentSection_].tempoMilliBpm != 0) {
            setTempoMilliBpm(song_->sections[currentSection_].tempoMilliBpm);
        }
        for (uint8_t i = 0; i < song_->numMappings; ++i) {
            const Mapping& m = song_->mappings[i];
            applyMapping(m, params_[m.param], tick, false);
        }
        updateMutes();
        updateGates();
        updateLayerGains(0);
        updateMix();
    }

    /// Replay the bar playing: every channel, the bar count and anything
    /// waiting for a later beat or bar move back to where the bar began.
    void rewindToBar(uint64_t tick) {
        if (tick < sectionStartTick_) return;
        const uint64_t bar = uint64_t(config::kTicksPerBeat) * currentBeatsPerBar();
        const uint64_t back = (tick - sectionStartTick_) % bar;
        if (back == 0) return;
        for (auto& ch : channels_) ch.rewind(back, tick);
        sectionStartTick_ += back;
        for (uint32_t i = 0; i < numPending_; ++i) {
            if (pending_[i].dueTick > tick) pending_[i].dueTick += back;
        }
    }

    /// A silent channel with something to play in the current section joins in time.
    void joinSection(uint32_t c) {
        if (currentSection_ == kNoIndex || channels_[c].isPlaying()) return;
        const Sequence& s = song_->sections[currentSection_].sequences[c];
        if (song_->isPlayable(s)) channels_[c].playSequence(&s, song_->patterns, sectionStartTick_);
    }

    void added(Addition what, uint8_t index) {
        switch (what) {
        case Addition::Pattern:
            if (index == song_->numPatterns && index < config::kMaxPatterns) ++song_->numPatterns;
            break;
        case Addition::Channel:
            if (index == song_->numChannels && index < config::kMaxChannels) {
                ++song_->numChannels;
                setUpChannel(index);
                applyColumnInstruments(index, 0);
                updateGates();
                updateMix();
            }
            break;
        case Addition::Layer:
            if (index == song_->numLayers && index < config::kMaxLayers) {
                ++song_->numLayers;
                layerMuted_[index] = false;
                layerGains_[index] = 1.0f;
            }
            break;
        case Addition::Section:
            if (index == song_->numSections && index < config::kMaxSections) ++song_->numSections;
            break;
        }
    }

    /// A sequence changed: the channel playing it carries on, and a channel
    /// that had nothing to play in the current section joins in time.
    void sequenceEdited(uint8_t section, uint8_t channel, SeqEdit op, uint32_t pos, uint64_t tick) {
        if (section != currentSection_) return;
        const Sequence& s = song_->sections[section].sequences[channel];
        Channel& ch = channels_[channel];
        if (ch.playsSequence(&s)) {
            if (s.length == 0) {
                ch.play(nullptr, 0);
            } else {
                ch.sequenceEdited(op, pos, tick);
            }
        } else if (!ch.isOneShot() && song_->isPlayable(s)) {
            ch.playSequence(&s, song_->patterns, sectionStartTick_);
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
        for (auto& ch : channels_) ch.setColumnMask(~0u);  // song playback hears every column
        const bool keepPhase = entry == Entry::KeepPhase && currentSection_ != kNoIndex;
        const uint64_t start = keepPhase ? sectionStartTick_ : tick;
        for (uint32_t c = 0; c < config::kMaxChannels; ++c) {
            Channel& ch = channels_[c];
            if (song_->isPlayable(s.sequences[c])) {
                if (!keepPhase) freshStart(c);  // carrying on (keep phase): as it sounds now
                ch.playSequence(&s.sequences[c], song_->patterns, start);
            } else if (!ch.isOneShot()) {
                ch.play(nullptr, 0);  // not in this section; leave stingers alone
            }
        }
        if (s.tempoMilliBpm != 0) setTempoMilliBpm(s.tempoMilliBpm);
        currentSection_ = index;
        sectionStartTick_ = start;
        updateGates();
    }

    uint32_t sampleRate_;
    TempoClock clock_;
    std::array<Channel, config::kMaxChannels> channels_;
    synth::OutputProcessor master_;
    PitchContext pitch_;
    bool tickPending_ = true;  // tick 0 starts at the first sample

    Song* song_ = nullptr;
    uint8_t monitor_ = kNoIndex;
    SpscQueue<Command, config::kCommandQueueSize> queue_;
    Pending pending_[config::kMaxPendingCommands] = {};
    uint32_t numPending_ = 0;
    uint32_t droppedCommands_ = 0;
    uint8_t currentSection_ = kNoIndex;
    uint64_t sectionStartTick_ = 0;

    // Parameters and mapping targets.
    float params_[config::kMaxParams] = {};
    float layerGains_[config::kMaxLayers] = {};  // set to 1 in the constructor
    bool layerMuted_[config::kMaxLayers] = {};
    bool hearAll_ = false;  ///< editor playback: every note, whatever the mix says
    uint32_t baseTempoMilliBpm_ = 120000;
    Ramp tempoScale_{1.0f};
    Ramp speed_{1.0f};  ///< Tape speed: multiplies tempo and pitch.
    Ramp masterCutoffLog2_;

    // Song swaps (see offer()).
    static constexpr uint8_t kSwapSlots = 4;
    std::atomic<SongSwap*> swapSlots_[kSwapSlots] = {};
    std::atomic<uint32_t> swapsTaken_{0};

    // Editor note previews.
    std::array<synth::WavetableSynth, config::kPreviewVoices> previews_;
    Channel::Timer previewTimer_;
    uint8_t previewInstrument_[config::kPreviewVoices] = {kNoIndex, kNoIndex, kNoIndex, kNoIndex};
};

} // namespace adaptive
