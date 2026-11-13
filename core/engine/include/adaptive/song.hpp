#pragma once

#include <adaptive/config.hpp>
#include <adaptive/pattern.hpp>

#include <cstdint>

/**
 * Adaptive song structure: sections, layers and game events, plus the commands
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
 * to the engine and for the actions a game event fires.
 */
struct Command {
    enum class Kind : uint8_t {
        None,
        GotoSection,  ///< a = section, b = bridge section or kNoIndex; uses `entry`
        PlayStinger,  ///< a = channel; plays `pattern` once
        SetLayerMute, ///< a = layer, b = 1 to mute, 0 to unmute
        FireEvent,    ///< a = event; runs the event's actions
        Stop,         ///< stop all channels (sounding notes release)
    };

    Kind kind = Kind::None;
    Quantize when = Quantize::Now;
    Entry entry = Entry::Restart;
    uint8_t a = 0;
    uint8_t b = kNoIndex;
    const Pattern* pattern = nullptr;

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
};

/// A named one-shot trigger (the name lives in the app). Fires its actions in order.
struct GameEvent {
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

    GameEvent events[config::kMaxEvents] = {};
    uint8_t numEvents = 0;
};

static_assert(config::kMaxChannels <= 32, "layer bitmasks are 32 bits");

} // namespace adaptive
