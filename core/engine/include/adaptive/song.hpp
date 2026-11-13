#pragma once

#include <adaptive/config.hpp>
#include <adaptive/pattern.hpp>

#include <cmath>
#include <cstdint>

/**
 * Adaptive song structure: sections, layers and events, plus the commands
 * that control playback. All plain data with fixed capacities; the engine
 * refers to sections, layers and events by index.
 */
namespace adaptive {

/// When a scheduled change takes effect. Beats and bars are counted from the
/// start of the current section.
enum class Quantize : uint8_t { Now, Beat, Bar };

/// Where channels pick up when a new section begins.
enum class Entry : uint8_t {
    Restart,    ///< Channels start their new sequences from the top.
    KeepPhase,  ///< Channels continue at the same position; the bar count carries on.
};

inline constexpr uint8_t kNoIndex = 0xFF;

/**
 * One adaptive state (explore, combat, ...): which sequence each channel plays.
 * A channel with no sequence is silent in this section; channels reserved for
 * stingers should have none.
 */
struct Section {
    const Sequence* sequences[config::kMaxChannels] = {};
    uint8_t beatsPerBar = 4;
    uint32_t tempoMilliBpm = 0;  ///< 0 keeps the current tempo.
    /// Only used when this section serves as a transition bridge: how long it
    /// plays before the target section starts.
    uint16_t bridgeBeats = 0;
};

/**
 * One thing to do, now or at a quantised time. Used both for commands posted
 * to the engine and for the actions an event fires.
 */
struct Command {
    enum class Kind : uint8_t {
        None,
        GotoSection,  ///< a = section, b = bridge section or kNoIndex; uses `entry`
        PlayStinger,  ///< a = channel; plays `pattern` once
        SetLayerMute, ///< a = layer, b = 1 to mute, 0 to unmute
        FireEvent,    ///< a = event; runs the event's actions
        Stop,         ///< stop all channels (sounding notes release)
        SetParam,     ///< a = parameter, value = 0..1; runs mappings
        SetKey,       ///< a = root pitch class, data = scale mask
        SetTempo,     ///< value = base tempo in BPM (scaled by any TempoScale mapping)
        SetCell,      ///< target[data][a] = cell: edit a pattern safely while it plays
        PreviewOn,    ///< b = preview slot; sound note `data` (MIDI) with channel a's patch
        PreviewOff,   ///< b = preview slot; release it
    };

    Kind kind = Kind::None;
    Quantize when = Quantize::Now;
    Entry entry = Entry::Restart;
    uint8_t a = 0;
    uint8_t b = kNoIndex;
    const Pattern* pattern = nullptr;
    float value = 0.0f;
    uint16_t data = 0;
    Pattern* target = nullptr;  ///< SetCell only. The app owns patterns and may edit them.
    Cell cell = {};             ///< SetCell only.

    static constexpr Command gotoSection(uint8_t section, Quantize when = Quantize::Bar,
                                         Entry entry = Entry::Restart, uint8_t bridge = kNoIndex) {
        return Command{Kind::GotoSection, when, entry, section, bridge, nullptr};
    }
    static constexpr Command playStinger(uint8_t channel, const Pattern* p, Quantize when = Quantize::Now) {
        return Command{Kind::PlayStinger, when, Entry::Restart, channel, 0, p};
    }
    static constexpr Command setLayerMute(uint8_t layer, bool muted, Quantize when = Quantize::Bar) {
        return Command{Kind::SetLayerMute, when, Entry::Restart, layer, uint8_t(muted ? 1 : 0), nullptr};
    }
    static constexpr Command fireEvent(uint8_t event) {
        return Command{Kind::FireEvent, Quantize::Now, Entry::Restart, event, 0, nullptr};
    }
    static constexpr Command stop() { return Command{Kind::Stop, Quantize::Now, Entry::Restart, 0, 0, nullptr}; }
    static constexpr Command setParam(uint8_t param, float v) {
        return Command{Kind::SetParam, Quantize::Now, Entry::Restart, param, 0, nullptr, v, 0};
    }
    static constexpr Command setKey(uint8_t root, uint16_t scale, Quantize when = Quantize::Bar) {
        return Command{Kind::SetKey, when, Entry::Restart, root, 0, nullptr, 0.0f, scale};
    }
    static constexpr Command setTempo(float bpm, Quantize when = Quantize::Beat) {
        return Command{Kind::SetTempo, when, Entry::Restart, 0, 0, nullptr, bpm, 0};
    }
    static constexpr Command setCell(Pattern* p, uint16_t row, uint8_t column, const Cell& c) {
        return Command{Kind::SetCell, Quantize::Now, Entry::Restart, column, 0, nullptr, 0.0f, row, p, c};
    }
    static constexpr Command previewOn(uint8_t channel, uint8_t slot, uint8_t midiNote, float volume = 1.0f) {
        return Command{Kind::PreviewOn, Quantize::Now, Entry::Restart, channel, slot, nullptr, volume, midiNote};
    }
    static constexpr Command previewOff(uint8_t slot) {
        return Command{Kind::PreviewOff, Quantize::Now, Entry::Restart, 0, slot, nullptr, 0.0f, 0};
    }
};

/**
 * Drives a target from a parameter. Data only: the engine evaluates
 * mappings whenever their parameter changes. If several mappings drive the
 * same target, the most recently evaluated one wins.
 */
struct Mapping {
    enum class Target : uint8_t {
        None,
        ChannelGain,    ///< index = channel; out = gain (1 = unity)
        LayerGain,      ///< index = layer; out = gain, multiplies channel gain
        ChannelCutoff,  ///< index = channel; out = Hz (channel low-pass filter)
        MasterCutoff,   ///< out = Hz (master post-filter)
        TempoScale,     ///< out = multiplier on the base tempo
        Transpose,      ///< out = semitones, rounded; applies to notes that start afterwards
        Threshold,      ///< fires `index` when the parameter rises past `threshold`,
                        ///< and `indexBelow` when it falls back below
    };
    enum class Curve : uint8_t {
        Linear,       ///< out = outMin + (outMax - outMin) * p
        Exponential,  ///< out = outMin * (outMax / outMin) ^ p; for Hz and tempo. Both must be > 0
    };

    uint8_t param = 0;
    Target target = Target::None;
    uint8_t index = 0;
    Curve curve = Curve::Linear;
    float outMin = 0.0f;  ///< Output at parameter 0.
    float outMax = 1.0f;  ///< Output at parameter 1.
    uint16_t rampMs = 30; ///< Glide time for gains, cutoffs and tempo.
    float threshold = 0.5f;
    uint8_t indexBelow = kNoIndex;

    float map(float p) const {
        if (p < 0.0f) p = 0.0f;
        if (p > 1.0f) p = 1.0f;
        if (curve == Curve::Exponential && outMin > 0.0f && outMax > 0.0f) {
            return outMin * std::exp2(p * std::log2(outMax / outMin));
        }
        return outMin + (outMax - outMin) * p;
    }
};

/// A named one-shot trigger (the name lives in the app). Fires its actions in order.
struct Event {
    Command actions[config::kMaxEventActions] = {};
    uint8_t numActions = 0;

    bool add(const Command& c) {
        if (numActions >= config::kMaxEventActions || c.kind == Command::Kind::FireEvent) return false;
        actions[numActions++] = c;
        return true;
    }
};

struct Song {
    Section sections[config::kMaxSections] = {};
    uint8_t numSections = 0;

    /// Each layer is a bitmask of channels, muted and unmuted together.
    uint32_t layers[config::kMaxLayers] = {};
    uint8_t numLayers = 0;

    Event events[config::kMaxEvents] = {};
    uint8_t numEvents = 0;

    Mapping mappings[config::kMaxMappings] = {};
    uint8_t numMappings = 0;

    bool addMapping(const Mapping& m) {
        if (numMappings >= config::kMaxMappings) return false;
        mappings[numMappings++] = m;
        return true;
    }
};

static_assert(config::kMaxChannels <= 32, "layer bitmasks are 32 bits");

} // namespace adaptive
