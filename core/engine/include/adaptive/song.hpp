#pragma once

#include <adaptive/config.hpp>
#include <adaptive/instrument.hpp>
#include <adaptive/pattern.hpp>
#include <adaptive/pitch.hpp>

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

/// What Command::added made available.
enum class Addition : uint8_t { Pattern, Channel, Section, Layer };

/// Parts of a cell, as bits (Command::clearCells, Song::clearCells).
inline constexpr uint8_t kCellNote = 1;    ///< note and accidental
inline constexpr uint8_t kCellVolume = 2;
inline constexpr uint8_t kCellEffect = 4;  ///< effect and its parameter
inline constexpr uint8_t kCellAll = kCellNote | kCellVolume | kCellEffect;

/**
 * One adaptive state (explore, combat, ...): which patterns each channel
 * plays. Each channel loops its own sequence, so they can have different
 * lengths. A channel with an empty sequence is silent in this section;
 * channels reserved for stingers should have none.
 */
struct Section {
    Sequence sequences[config::kMaxChannels] = {};
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
        PlayStinger,  ///< a = channel; plays song pattern `data` once
        SetLayerMute, ///< a = layer, b = 1 to mute, 0 to unmute
        FireEvent,    ///< a = event; runs the event's actions
        Stop,         ///< stop all channels (sounding notes release)
        SetParam,     ///< a = parameter, value = 0..1; runs mappings
        SetKey,       ///< a = root pitch class, data = scale mask
        SetTempo,     ///< value = base tempo in BPM (scaled by any TempoScale mapping)
        SetCell,      ///< song pattern b, row data, column a = cell: edit safely while it plays
        PreviewOn,    ///< b = preview slot; sound note `data` (MIDI) with the song's instrument a
        PreviewOff,   ///< b = preview slot; release it
        SoloPattern,  ///< stop everything, then loop song pattern b alone on channel a; data = column mask
        SetColumnMask,///< channel a plays only the columns in bitmask `data`
        SetInstrumentParam, ///< song instrument a, parameter b = value; updates voices using it
        AddInstrument,///< make song instrument a (already written by the app) available
        AssignInstrument, ///< channel a, column b plays song instrument `data` (kNoIndex: none)
        SetMonitor,   ///< render() copies instrument a's voices to its monitor output (kNoIndex: off)
        SetPatternShape, ///< song pattern b gets `data` rows at speed a (ticks per row); see Song::setPatternShape
        ClearCells,   ///< in song pattern b, column a, rows data&0xFF..data>>8: empty the parts in `parts`
        ScaleRows,    ///< song pattern b: row r moves to r * a / data; see Song::scaleRows
        Added,        ///< item b of kind a (an Addition), already written past the end, now counts
        EditSequence, ///< section a, channel b: SeqEdit `parts` at entry data&0xFF, pattern data>>8
        SetSection,   ///< section a: beats per bar b, tempo `value` BPM (0: keep), bridge beats `data`
        SetColumnPitch, ///< channel a, column b reads notes in PitchMode `data` (cells converted)
        SetColumnGate,  ///< channel a, column b: gated by parameter `data` (kNoIndex: none) at `value`
        SetLayerColumns,///< layer a: channel b's columns are the bits of `data`
        RemoveLayer,    ///< take out layer a (and what refers to it)
        HearAll,        ///< a != 0: ignore gates, layer mutes and layer gains (hear every note)
        SwapSong,       ///< take the song offered in swap slot a (see Engine::offer)
        SetChannelVolume, ///< channel a's fader = value (0..1); a song edit
        SetChannelMute, ///< channel a muted while b != 0 (at once, notes keep time silently)
        SetChannelSolo, ///< channel a soloed while b != 0: unsoloed channels are silent
        SetChannelPause,///< channel a paused while b != 0: starts no notes, keeps its place
        RestartChannel, ///< channel a starts what it plays over, from its first entry
        SetColumns,   ///< channel a gets b columns; see Song::setColumns
        RemoveColumn, ///< remove channel a's column b; see Song::removeColumn
    };

    Kind kind = Kind::None;
    Quantize when = Quantize::Now;
    Entry entry = Entry::Restart;
    uint8_t a = 0;
    uint8_t b = kNoIndex;
    float value = 0.0f;
    uint16_t data = 0;
    Cell cell = {};  ///< SetCell only.
    uint8_t parts = 0;  ///< ClearCells only: CellPart bits.

    static constexpr Command gotoSection(uint8_t section, Quantize when = Quantize::Bar,
                                         Entry entry = Entry::Restart, uint8_t bridge = kNoIndex) {
        return Command{Kind::GotoSection, when, entry, section, bridge};
    }
    static constexpr Command playStinger(uint8_t channel, uint8_t pattern, Quantize when = Quantize::Now) {
        return Command{Kind::PlayStinger, when, Entry::Restart, channel, 0, 0.0f, pattern};
    }
    static constexpr Command setLayerMute(uint8_t layer, bool muted, Quantize when = Quantize::Bar) {
        return Command{Kind::SetLayerMute, when, Entry::Restart, layer, uint8_t(muted ? 1 : 0)};
    }
    static constexpr Command fireEvent(uint8_t event) {
        return Command{Kind::FireEvent, Quantize::Now, Entry::Restart, event, 0};
    }
    static constexpr Command stop() { return Command{Kind::Stop, Quantize::Now, Entry::Restart, 0, 0}; }
    static constexpr Command setParam(uint8_t param, float v) {
        return Command{Kind::SetParam, Quantize::Now, Entry::Restart, param, 0, v, 0};
    }
    static constexpr Command setKey(uint8_t root, uint16_t scale, Quantize when = Quantize::Bar) {
        return Command{Kind::SetKey, when, Entry::Restart, root, 0, 0.0f, scale};
    }
    static constexpr Command setTempo(float bpm, Quantize when = Quantize::Beat) {
        return Command{Kind::SetTempo, when, Entry::Restart, 0, 0, bpm, 0};
    }
    /// Edit a cell of song pattern `pattern` safely while it plays.
    static constexpr Command setCell(uint8_t pattern, uint16_t row, uint8_t column, const Cell& c) {
        return Command{Kind::SetCell, Quantize::Now, Entry::Restart, column, pattern, 0.0f, row, c};
    }
    static constexpr Command previewOn(uint8_t instrument, uint8_t slot, uint8_t midiNote, float volume = 1.0f) {
        return Command{Kind::PreviewOn, Quantize::Now, Entry::Restart, instrument, slot, volume, midiNote};
    }
    /// Edit song instrument `index` safely while it plays.
    static constexpr Command setInstrumentParam(uint8_t index, Instrument::Param p, float value) {
        return Command{Kind::SetInstrumentParam, Quantize::Now, Entry::Restart, index, static_cast<uint8_t>(p),
                       value};
    }
    /// Make `song.instruments[index]` playable. The app writes that entry
    /// first: entries at or past `numInstruments` are never read by the
    /// audio thread, so writing one is safe while it plays.
    static constexpr Command addInstrument(uint8_t index) {
        return Command{Kind::AddInstrument, Quantize::Now, Entry::Restart, index, 0};
    }
    /// Channel `channel`'s column `column` plays song instrument `instrument`
    /// from now on; its voice takes the instrument's settings straight away.
    static constexpr Command assignInstrument(uint8_t channel, uint8_t column, uint8_t instrument) {
        return Command{Kind::AssignInstrument, Quantize::Now, Entry::Restart, channel, column, 0.0f, instrument};
    }
    /// Listen to one instrument on render()'s monitor output (kNoIndex: none).
    static constexpr Command setMonitor(uint8_t instrument) {
        return Command{Kind::SetMonitor, Quantize::Now, Entry::Restart, instrument, 0};
    }
    /// Loop song pattern `pattern` on its own on `channel` (e.g. the one being
    /// edited). `columnMask` selects which of its columns are heard (bit 0 = column 0).
    static constexpr Command soloPattern(uint8_t channel, uint8_t pattern, uint16_t columnMask = 0xFFFF) {
        return Command{Kind::SoloPattern, Quantize::Now, Entry::Restart, channel, pattern, 0.0f, columnMask};
    }
    static constexpr Command setColumnMask(uint8_t channel, uint16_t columnMask) {
        return Command{Kind::SetColumnMask, Quantize::Now, Entry::Restart, channel, 0, 0.0f, columnMask};
    }
    // Pattern and channel shape edits, safe while playing. The app makes the
    // same change to its own copy with the Song method of the same name.
    static constexpr Command setPatternShape(uint8_t pattern, uint16_t rows, uint8_t ticksPerRow) {
        return Command{Kind::SetPatternShape, Quantize::Now, Entry::Restart, ticksPerRow, pattern, 0.0f, rows};
    }
    static constexpr Command clearColumn(uint8_t pattern, uint8_t column) {
        return clearCells(pattern, column, 0, config::kMaxRows - 1, kCellAll);
    }
    /// Rows `first` to `last` (inclusive) of a column; `parts`: CellPart bits.
    static constexpr Command clearCells(uint8_t pattern, uint8_t column, uint32_t first, uint32_t last,
                                        uint8_t parts) {
        Command c{Kind::ClearCells, Quantize::Now, Entry::Restart, column, pattern, 0.0f,
                  static_cast<uint16_t>((first & 0xFF) | ((last & 0xFF) << 8))};
        c.parts = parts;
        return c;
    }
    /**
     * Item `index` of kind `what` was written just past the end of its table
     * (a slot the engine doesn't read yet) and now counts. `index` must be the
     * table's current count.
     */
    static constexpr Command added(Addition what, uint8_t index) {
        return Command{Kind::Added, Quantize::Now, Entry::Restart, static_cast<uint8_t>(what), index};
    }
    static constexpr Command editSequence(uint8_t section, uint8_t channel, SeqEdit op, uint8_t pos,
                                          uint8_t pattern = 0) {
        Command c{Kind::EditSequence, Quantize::Now, Entry::Restart, section, channel, 0.0f,
                  static_cast<uint16_t>(pos | (pattern << 8))};
        c.parts = static_cast<uint8_t>(op);
        return c;
    }
    static constexpr Command setSection(uint8_t section, float bpm, uint8_t beatsPerBar, uint16_t bridgeBeats) {
        return Command{Kind::SetSection, Quantize::Now, Entry::Restart, section, beatsPerBar, bpm, bridgeBeats};
    }
    static constexpr Command setColumnPitch(uint8_t channel, uint8_t column, PitchMode mode) {
        return Command{Kind::SetColumnPitch, Quantize::Now, Entry::Restart, channel, column, 0.0f,
                       static_cast<uint16_t>(mode)};
    }
    static constexpr Command setColumnGate(uint8_t channel, uint8_t column, uint8_t param, float threshold) {
        return Command{Kind::SetColumnGate, Quantize::Now, Entry::Restart, channel, column, threshold, param};
    }
    static constexpr Command setLayerColumns(uint8_t layer, uint8_t channel, uint16_t columnBits) {
        return Command{Kind::SetLayerColumns, Quantize::Now, Entry::Restart, layer, channel, 0.0f, columnBits};
    }
    static constexpr Command setChannelVolume(uint8_t channel, float volume) {
        return Command{Kind::SetChannelVolume, Quantize::Now, Entry::Restart, channel, 0, volume, 0};
    }
    static constexpr Command setChannelMute(uint8_t channel, bool on) {
        return Command{Kind::SetChannelMute, Quantize::Now, Entry::Restart, channel, uint8_t(on ? 1 : 0)};
    }
    static constexpr Command setChannelSolo(uint8_t channel, bool on) {
        return Command{Kind::SetChannelSolo, Quantize::Now, Entry::Restart, channel, uint8_t(on ? 1 : 0)};
    }
    static constexpr Command setChannelPause(uint8_t channel, bool on, Quantize when = Quantize::Now) {
        return Command{Kind::SetChannelPause, when, Entry::Restart, channel, uint8_t(on ? 1 : 0)};
    }
    static constexpr Command restartChannel(uint8_t channel, Quantize when = Quantize::Bar) {
        return Command{Kind::RestartChannel, when, Entry::Restart, channel, 0};
    }
    static constexpr Command swapSong(uint8_t slot) {
        return Command{Kind::SwapSong, Quantize::Now, Entry::Restart, slot, 0};
    }
    static constexpr Command hearAll(bool on) {
        return Command{Kind::HearAll, Quantize::Now, Entry::Restart, static_cast<uint8_t>(on ? 1 : 0), 0};
    }
    static constexpr Command removeLayer(uint8_t layer) {
        return Command{Kind::RemoveLayer, Quantize::Now, Entry::Restart, layer, 0};
    }
    static constexpr Command scaleRows(uint8_t pattern, uint8_t num, uint16_t den) {
        return Command{Kind::ScaleRows, Quantize::Now, Entry::Restart, num, pattern, 0.0f, den};
    }
    static constexpr Command setColumns(uint8_t channel, uint8_t count) {
        return Command{Kind::SetColumns, Quantize::Now, Entry::Restart, channel, count};
    }
    static constexpr Command removeColumn(uint8_t channel, uint8_t column) {
        return Command{Kind::RemoveColumn, Quantize::Now, Entry::Restart, channel, column};
    }
    static constexpr Command previewOff(uint8_t slot) {
        return Command{Kind::PreviewOff, Quantize::Now, Entry::Restart, 0, slot, 0.0f, 0};
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

/**
 * How an edit renumbered the song's tables: for each old index, the new one
 * (kNoIndex: removed). Things added at the end need no entry. Used to keep
 * playback in place when the engine is handed a changed copy of its song.
 */
struct Renumbering {
    uint8_t channels[config::kMaxChannels];
    uint8_t patterns[config::kMaxPatterns];
    uint8_t sections[config::kMaxSections];
    uint8_t layers[config::kMaxLayers];

    Renumbering() {
        identity(channels, config::kMaxChannels);
        identity(patterns, config::kMaxPatterns);
        identity(sections, config::kMaxSections);
        identity(layers, config::kMaxLayers);
    }

    /// Entry `index` of the table that `map` renumbers was taken out.
    static void removed(uint8_t* map, uint32_t size, uint32_t index) {
        for (uint32_t i = 0; i < size; ++i) {
            if (map[i] == kNoIndex) continue;
            if (map[i] == index) {
                map[i] = kNoIndex;
            } else if (map[i] > index) {
                --map[i];
            }
        }
    }

    /// From the new numbering back to the old (what was removed is new again).
    Renumbering inverse() const {
        Renumbering r;
        invert(channels, r.channels, config::kMaxChannels);
        invert(patterns, r.patterns, config::kMaxPatterns);
        invert(sections, r.sections, config::kMaxSections);
        invert(layers, r.layers, config::kMaxLayers);
        return r;
    }

    /// This renumbering followed by `next`.
    Renumbering then(const Renumbering& next) const {
        Renumbering r;
        compose(channels, next.channels, r.channels, config::kMaxChannels);
        compose(patterns, next.patterns, r.patterns, config::kMaxPatterns);
        compose(sections, next.sections, r.sections, config::kMaxSections);
        compose(layers, next.layers, r.layers, config::kMaxLayers);
        return r;
    }

    bool isIdentity() const {
        const Renumbering id;
        for (uint32_t i = 0; i < config::kMaxChannels; ++i) if (channels[i] != id.channels[i]) return false;
        for (uint32_t i = 0; i < config::kMaxPatterns; ++i) if (patterns[i] != id.patterns[i]) return false;
        for (uint32_t i = 0; i < config::kMaxSections; ++i) if (sections[i] != id.sections[i]) return false;
        for (uint32_t i = 0; i < config::kMaxLayers; ++i) if (layers[i] != id.layers[i]) return false;
        return true;
    }

private:
    static void identity(uint8_t* map, uint32_t size) {
        for (uint32_t i = 0; i < size; ++i) map[i] = static_cast<uint8_t>(i);
    }
    static void invert(const uint8_t* map, uint8_t* out, uint32_t size) {
        for (uint32_t i = 0; i < size; ++i) out[i] = kNoIndex;
        for (uint32_t i = 0; i < size; ++i) {
            if (map[i] < size) out[map[i]] = static_cast<uint8_t>(i);
        }
    }
    static void compose(const uint8_t* first, const uint8_t* second, uint8_t* out, uint32_t size) {
        for (uint32_t i = 0; i < size; ++i) out[i] = first[i] < size ? second[first[i]] : kNoIndex;
    }
};

/// How a channel plays: its pitch mode, how many columns (voices) it has,
/// and each column's instrument.
struct ChannelSetup {
    uint8_t numColumns = 1;
    /// Song instrument per column; kNoIndex leaves the voice as it is.
    uint8_t instruments[config::kMaxColumns];
    /// How each column's notes are read.
    PitchMode pitchModes[config::kMaxColumns];
    /// Per column: the parameter that gates it (kNoIndex: it always plays),
    /// and the value it must be at least for the column to start notes.
    uint8_t gateParams[config::kMaxColumns];
    float gateThresholds[config::kMaxColumns];
    /// The mixer fader, 0..1 (heard as its square; 1 is unity).
    float volume = 1.0f;

    ChannelSetup() {
        for (uint32_t c = 0; c < config::kMaxColumns; ++c) {
            instruments[c] = kNoIndex;
            pitchModes[c] = PitchMode::Absolute;
            gateParams[c] = kNoIndex;
            gateThresholds[c] = 0.0f;
        }
    }
};

/// A layer's bit for a channel's column (Song::layers).
constexpr uint32_t layerBit(uint32_t channel, uint32_t column) {
    return 1u << (channel * config::kMaxColumns + column);
}
/// All of a channel's columns in a layer mask.
constexpr uint32_t layerChannelBits(uint32_t channel) {
    return ((1u << config::kMaxColumns) - 1u) << (channel * config::kMaxColumns);
}
static_assert(config::kMaxChannels * config::kMaxColumns <= 32, "layers are 32-bit masks of columns");

/**
 * Everything the engine plays, as plain data with fixed capacities. Parts
 * refer to each other by index (patterns, instruments, sections, layers,
 * events, parameters), so a song can be copied, saved and loaded as is.
 * Names and other editing data live outside it (see the song file format).
 */
struct Song {
    /// Tempo and key the song starts in, and the section Play starts.
    uint32_t tempoMilliBpm = 120000;
    Key homeKey = {0, scales::kMajor};
    uint8_t startSection = 0;

    ChannelSetup channels[config::kMaxChannels] = {};
    uint8_t numChannels = 0;

    /// Every pattern, whatever channel it belongs to (Pattern::channel).
    Pattern patterns[config::kMaxPatterns] = {};
    uint8_t numPatterns = 0;

    Section sections[config::kMaxSections] = {};
    uint8_t numSections = 0;

    /// Each layer is a bitmask of columns (layerBit), muted and unmuted together.
    uint32_t layers[config::kMaxLayers] = {};
    uint8_t numLayers = 0;

    Event events[config::kMaxEvents] = {};
    uint8_t numEvents = 0;

    /// Parameters the app sets (0..1); mappings turn them into changes.
    /// Each starts at its initial value when the song is set.
    uint8_t numParams = 0;
    float paramInitial[config::kMaxParams] = {};

    Mapping mappings[config::kMaxMappings] = {};
    uint8_t numMappings = 0;

    /// Synth patches; channels choose one per column.
    Instrument instruments[config::kMaxInstruments] = {};
    /// Editing hint per instrument: the PitchMode a column switches to when
    /// given this instrument (kNoIndex: none). The engine doesn't use it.
    uint8_t instrumentPitch[config::kMaxInstruments] = {};
    uint8_t numInstruments = 0;

    /// Add a channel; returns its index, or kNoIndex when the table is full.
    uint8_t addChannel(PitchMode mode, uint8_t numColumns) {
        if (numChannels >= config::kMaxChannels) return kNoIndex;
        ChannelSetup& c = channels[numChannels];
        c = ChannelSetup();
        for (auto& m : c.pitchModes) m = mode;
        c.numColumns = numColumns < 1 ? 1 : (numColumns > config::kMaxColumns ? config::kMaxColumns : numColumns);
        return numChannels++;
    }
    /// Add an empty pattern for `channel`; returns its index, or kNoIndex when full.
    uint8_t addPattern(uint8_t channel, uint16_t numRows = 16, uint8_t ticksPerRow = 6) {
        if (numPatterns >= config::kMaxPatterns) return kNoIndex;
        Pattern& p = patterns[numPatterns];
        p = Pattern();
        p.channel = channel;
        p.numRows = numRows;
        p.ticksPerRow = ticksPerRow;
        return numPatterns++;
    }
    /// Add an empty section (nothing plays in it); returns its index, or kNoIndex when full.
    uint8_t addSection() {
        if (numSections >= config::kMaxSections) return kNoIndex;
        sections[numSections] = Section();
        return numSections++;
    }

    /**
     * Change the sequence `channel` plays in `section`. Inserted and set
     * patterns must belong to the channel. False if the change can't be made.
     */
    bool editSequence(uint32_t section, uint32_t channel, SeqEdit op, uint32_t pos, uint32_t pattern) {
        if (section >= numSections || channel >= numChannels) return false;
        Sequence& s = sections[section].sequences[channel];
        if (op != SeqEdit::Remove && (pattern >= numPatterns || patterns[pattern].channel != channel)) return false;
        switch (op) {
        case SeqEdit::Insert:
            if (pos > s.length || s.length >= config::kMaxSequenceLength) return false;
            for (uint32_t i = s.length; i > pos; --i) s.entries[i] = s.entries[i - 1];
            s.entries[pos] = static_cast<uint8_t>(pattern);
            ++s.length;
            return true;
        case SeqEdit::Remove:
            if (pos >= s.length) return false;
            for (uint32_t i = pos; i + 1 < s.length; ++i) s.entries[i] = s.entries[i + 1];
            --s.length;
            return true;
        case SeqEdit::Set:
            if (pos >= s.length) return false;
            s.entries[pos] = static_cast<uint8_t>(pattern);
            return true;
        }
        return false;
    }

    /// Section timing: tempo in milli-BPM (0 keeps the tempo), beats per bar (1..32), bridge beats (0: not a bridge).
    bool setSectionTiming(uint32_t section, uint32_t milliBpm, uint32_t beatsPerBar, uint32_t bridgeBeats) {
        if (section >= numSections || beatsPerBar < 1 || beatsPerBar > 32 || bridgeBeats > 0xFFFF ||
            milliBpm > 999000) {
            return false;
        }
        Section& s = sections[section];
        s.tempoMilliBpm = milliBpm;
        s.beatsPerBar = static_cast<uint8_t>(beatsPerBar);
        s.bridgeBeats = static_cast<uint16_t>(bridgeBeats);
        return true;
    }

    /**
     * `channel`'s `column` reads its notes in `mode` from now on. Notes
     * already written in it, in all the channel's patterns, are rewritten so
     * they sound the same in the home key (fixed and key-following notes are
     * the same numbers; degrees go to and from MIDI notes). Arpeggio steps
     * change between semitones and scale steps too, measured from the note
     * they arpeggiate (the column's last note above them, else its first);
     * semitones that fall between scale notes go to the nearest. A note with
     * no equivalent (out of range) stays as it is.
     */
    bool setColumnPitch(uint32_t channel, uint32_t column, PitchMode mode) {
        if (channel >= numChannels || column >= config::kMaxColumns || static_cast<uint32_t>(mode) > 2) return false;
        PitchMode& current = channels[channel].pitchModes[column];
        const bool fromDegree = current == PitchMode::Degree;
        const bool toDegree = mode == PitchMode::Degree;
        if (fromDegree != toDegree) {
            PitchContext home;
            home.home = home.current = homeKey;
            for (uint32_t p = 0; p < numPatterns; ++p) {
                if (patterns[p].channel != channel) continue;
                // MIDI note the arpeggios are measured from: the last note, else the first.
                int base = 60 + homeKey.root;
                for (uint32_t r = 0; r < config::kMaxRows; ++r) {
                    const Cell& cell = patterns[p].cells[r][column];
                    if (cell.isNote()) {
                        base = fromDegree ? resolvePitch(PitchMode::Degree, cell.note, cell.accidental, home) : cell.note;
                        break;
                    }
                }
                for (uint32_t r = 0; r < config::kMaxRows; ++r) {
                    Cell& cell = patterns[p].cells[r][column];
                    if (cell.isNote()) {
                        base = fromDegree ? resolvePitch(PitchMode::Degree, cell.note, cell.accidental, home) : cell.note;
                    }
                    if (cell.effect == fx::kArpeggio && cell.effectParam != 0) {
                        cell.effectParam = convertArpeggio(cell.effectParam, base, toDegree, home);
                    }
                    if (!cell.isNote()) continue;
                    if (toDegree) {
                        uint8_t note = 0;
                        int8_t accidental = 0;
                        if (encodeDegree(cell.note, homeKey, note, accidental)) {
                            cell.note = note;
                            cell.accidental = accidental;
                        }
                    } else {
                        cell.note = static_cast<uint8_t>(resolvePitch(PitchMode::Degree, cell.note, cell.accidental, home));
                        cell.accidental = 0;
                    }
                }
            }
        }
        current = mode;
        return true;
    }

    /// Arpeggio parameter 0xy measured from MIDI note `base`, from semitones
    /// to scale steps in the home key (`toDegree`) or back.
    static uint8_t convertArpeggio(uint8_t param, int base, bool toDegree, const PitchContext& home) {
        const int size = scaleSize(home.home.scale);
        uint8_t baseNote = 0;
        int8_t baseAcc = 0;
        if (!encodeDegree(base, home.home, baseNote, baseAcc)) return param;
        const int baseIndex = (baseNote / 12) * size + baseNote % 12;
        auto convert = [&](int step) {
            int out = step;
            if (toDegree) {
                uint8_t n = 0;
                int8_t acc = 0;
                if (encodeDegree(base + step, home.home, n, acc)) out = (n / 12) * size + n % 12 - baseIndex;
            } else {
                out = resolveDegree(baseNote / 12, baseNote % 12 + step, baseAcc, home) -
                      resolveDegree(baseNote / 12, baseNote % 12, baseAcc, home);
            }
            return out < 0 ? 0 : (out > 15 ? 15 : out);
        };
        return static_cast<uint8_t>((convert(param >> 4) << 4) | convert(param & 0x0F));
    }

    /// `channel`'s mixer fader (0..1).
    bool setChannelVolume(uint32_t channel, float volume) {
        if (channel >= numChannels || !(volume >= 0.0f && volume <= 1.0f)) return false;
        channels[channel].volume = volume;
        return true;
    }

    /// Gate `channel`'s `column` on parameter `param` (kNoIndex: always plays) at `threshold` (0..1).
    bool setColumnGate(uint32_t channel, uint32_t column, uint32_t param, float threshold) {
        if (channel >= numChannels || column >= config::kMaxColumns || (param != kNoIndex && param >= numParams) ||
            !(threshold >= 0.0f && threshold <= 1.0f)) {
            return false;
        }
        channels[channel].gateParams[column] = static_cast<uint8_t>(param);
        channels[channel].gateThresholds[column] = threshold;
        return true;
    }

    /// True if `column` of `channel` may start notes with parameters `values`.
    bool gateOpen(uint32_t channel, uint32_t column, const float* values) const {
        const uint8_t p = channels[channel].gateParams[column];
        return p >= numParams || values[p] >= channels[channel].gateThresholds[column];
    }

    /// Add an empty layer; returns its index, or kNoIndex when full.
    uint8_t addLayer() {
        if (numLayers >= config::kMaxLayers) return kNoIndex;
        layers[numLayers] = 0;
        return numLayers++;
    }

    /// Layer `layer` holds the columns of `channel` in `columnBits` (bit c = column c), and its other channels' as before.
    bool setLayerColumns(uint32_t layer, uint32_t channel, uint32_t columnBits) {
        if (layer >= numLayers || channel >= config::kMaxChannels || columnBits >= (1u << config::kMaxColumns)) {
            return false;
        }
        layers[layer] = (layers[layer] & ~layerChannelBits(channel)) | (columnBits << (channel * config::kMaxColumns));
        return true;
    }

    /**
     * Take out layer `layer`. Mappings that drive it and event actions that
     * mute it go too; later layers move down one.
     */
    bool removeLayer(uint32_t layer, Renumbering* r = nullptr) {
        if (layer >= numLayers) return false;
        if (r != nullptr) Renumbering::removed(r->layers, config::kMaxLayers, layer);
        for (uint32_t l = layer; l + 1 < numLayers; ++l) layers[l] = layers[l + 1];
        layers[--numLayers] = 0;
        uint8_t kept = 0;
        for (uint8_t i = 0; i < numMappings; ++i) {
            Mapping m = mappings[i];
            if (m.target == Mapping::Target::LayerGain) {
                if (m.index == layer) continue;
                if (m.index > layer) --m.index;
            }
            mappings[kept++] = m;
        }
        numMappings = kept;
        for (uint8_t e = 0; e < numEvents; ++e) {
            Event& ev = events[e];
            uint8_t n = 0;
            for (uint8_t a = 0; a < ev.numActions; ++a) {
                Command c = ev.actions[a];
                if (c.kind == Command::Kind::SetLayerMute) {
                    if (c.a == layer) continue;
                    if (c.a > layer) --c.a;
                }
                ev.actions[n++] = c;
            }
            ev.numActions = n;
        }
        return true;
    }

    /**
     * Take out pattern `index`: its sequence entries and the stinger actions
     * that play it go too, and later patterns move down. `r` (if given)
     * records the renumbering.
     */
    bool removePattern(uint32_t index, Renumbering* r = nullptr) {
        if (index >= numPatterns) return false;
        for (uint32_t i = index; i + 1 < numPatterns; ++i) patterns[i] = patterns[i + 1];
        patterns[--numPatterns] = Pattern();
        for (uint8_t sec = 0; sec < numSections; ++sec) {
            for (auto& seq : sections[sec].sequences) {
                uint8_t n = 0;
                for (uint8_t e = 0; e < seq.length; ++e) {
                    const uint8_t p = seq.entries[e];
                    if (p == index) continue;
                    seq.entries[n++] = p > index ? static_cast<uint8_t>(p - 1) : p;
                }
                seq.length = n;
            }
        }
        filterActions([index](Command& c) {
            if (c.kind != Command::Kind::PlayStinger) return true;
            if (c.data == index) return false;
            if (c.data > index) --c.data;
            return true;
        });
        if (r != nullptr) Renumbering::removed(r->patterns, config::kMaxPatterns, index);
        return true;
    }

    /**
     * Take out section `index`: actions that go to it go too (as a bridge,
     * they go straight to their section). Later sections move down.
     */
    bool removeSection(uint32_t index, Renumbering* r = nullptr) {
        if (index >= numSections) return false;
        for (uint32_t i = index; i + 1 < numSections; ++i) sections[i] = sections[i + 1];
        sections[--numSections] = Section();
        filterActions([index](Command& c) {
            if (c.kind != Command::Kind::GotoSection) return true;
            if (c.a == index) return false;
            if (c.a > index) --c.a;
            if (c.b == index) {
                c.b = kNoIndex;
            } else if (c.b != kNoIndex && c.b > index) {
                --c.b;
            }
            return true;
        });
        if (startSection == index) {
            startSection = 0;
        } else if (startSection > index && startSection != kNoIndex) {
            --startSection;
        }
        if (r != nullptr) Renumbering::removed(r->sections, config::kMaxSections, index);
        return true;
    }

    /**
     * Take out channel `index` with its patterns, its place in layers, and
     * the mappings and stinger actions aimed at it. Later channels move down.
     */
    bool removeChannel(uint32_t index, Renumbering* r = nullptr) {
        if (index >= numChannels) return false;
        for (uint32_t p = numPatterns; p-- > 0;) {
            if (patterns[p].channel == index) removePattern(p, r);
        }
        for (uint32_t p = 0; p < numPatterns; ++p) {
            if (patterns[p].channel > index) --patterns[p].channel;
        }
        for (uint32_t i = index; i + 1 < numChannels; ++i) channels[i] = channels[i + 1];
        channels[--numChannels] = ChannelSetup();
        for (uint8_t sec = 0; sec < config::kMaxSections; ++sec) {
            Sequence* seqs = sections[sec].sequences;
            for (uint32_t i = index; i + 1 < config::kMaxChannels; ++i) seqs[i] = seqs[i + 1];
            seqs[config::kMaxChannels - 1] = Sequence();
        }
        const uint32_t below = layerBit(index, 0) - 1u;
        for (uint8_t l = 0; l < config::kMaxLayers; ++l) {
            const uint64_t above = uint64_t(layers[l]) >> ((index + 1) * config::kMaxColumns);
            layers[l] = (layers[l] & below) | static_cast<uint32_t>(above << (index * config::kMaxColumns));
        }
        uint8_t kept = 0;
        for (uint8_t i = 0; i < numMappings; ++i) {
            Mapping m = mappings[i];
            if (m.target == Mapping::Target::ChannelGain || m.target == Mapping::Target::ChannelCutoff) {
                if (m.index == index) continue;
                if (m.index > index) --m.index;
            }
            mappings[kept++] = m;
        }
        numMappings = kept;
        filterActions([index](Command& c) {
            if (!aimsAtChannel(c)) return true;
            if (c.a == index) return false;
            if (c.a > index) --c.a;
            return true;
        });
        if (r != nullptr) Renumbering::removed(r->channels, config::kMaxChannels, index);
        return true;
    }

    /// True for commands whose `a` is a channel.
    static bool aimsAtChannel(const Command& c) {
        switch (c.kind) {
        case Command::Kind::PlayStinger:
        case Command::Kind::SetChannelVolume:
        case Command::Kind::SetChannelMute:
        case Command::Kind::SetChannelSolo:
        case Command::Kind::SetChannelPause:
        case Command::Kind::RestartChannel:
            return true;
        default:
            return false;
        }
    }

    /// Keep the event actions for which `keep` (which may change them) says true.
    template<typename Fn>
    void filterActions(Fn&& keep) {
        for (uint8_t e = 0; e < numEvents; ++e) {
            Event& ev = events[e];
            uint8_t n = 0;
            for (uint8_t a = 0; a < ev.numActions; ++a) {
                Command c = ev.actions[a];
                if (keep(c)) ev.actions[n++] = c;
            }
            ev.numActions = n;
        }
    }

    /// Add an instrument; returns its index, or kNoIndex when the table is full.
    uint8_t addInstrument(const Instrument& i) {
        if (numInstruments >= config::kMaxInstruments) return kNoIndex;
        instruments[numInstruments] = i;
        instrumentPitch[numInstruments] = kNoIndex;
        return numInstruments++;
    }
    /// Use instrument `index` on every column of `channel`.
    void assign(uint8_t channel, uint8_t index) {
        if (channel < config::kMaxChannels) {
            for (auto& c : channels[channel].instruments) c = index;
        }
    }
    /// Instrument played by `channel`'s `column`, or kNoIndex.
    uint8_t columnInstrument(uint32_t channel, uint32_t column) const {
        return channel < config::kMaxChannels && column < config::kMaxColumns
                   ? channels[channel].instruments[column] : kNoIndex;
    }

    /**
     * Give pattern `index` `rows` rows (1..kMaxRows) at `ticksPerRow` (>= 1).
     * Rows past the end keep their cells, so making it longer again brings
     * them back (song files store only the rows inside the pattern).
     */
    bool setPatternShape(uint32_t index, uint32_t rows, uint32_t ticksPerRow) {
        if (index >= numPatterns || rows < 1 || rows > config::kMaxRows || ticksPerRow < 1 || ticksPerRow > 255) {
            return false;
        }
        patterns[index].numRows = static_cast<uint16_t>(rows);
        patterns[index].ticksPerRow = static_cast<uint8_t>(ticksPerRow);
        return true;
    }

    /// Empty every row of `column` in pattern `index`.
    bool clearColumn(uint32_t index, uint32_t column) {
        if (index >= numPatterns) return false;
        return clearCells(index, column, 0, patterns[index].numRows - 1, kCellAll);
    }

    /// Empty the `parts` (CellPart bits) of rows `first` to `last` (inclusive) of a column.
    bool clearCells(uint32_t index, uint32_t column, uint32_t first, uint32_t last, uint8_t parts) {
        if (index >= numPatterns || column >= config::kMaxColumns || first > last) return false;
        Pattern& p = patterns[index];
        if (last >= p.numRows) last = p.numRows - 1u;
        for (uint32_t r = first; r <= last; ++r) {
            Cell& c = p.cells[r][column];
            if (parts & kCellNote) {
                c.note = Cell::kNoteEmpty;
                c.accidental = 0;
            }
            if (parts & kCellVolume) c.volume = Cell::kVolumeDefault;
            if (parts & kCellEffect) {
                c.effect = fx::kNone;
                c.effectParam = 0;
            }
        }
        return true;
    }

    /**
     * Stretch or squash pattern `index`: row r moves to r * num / den, rows
     * that don't land on a whole row are dropped, and the gaps are empty.
     * The pattern gets ceil(rows * num / den) rows; false if that is over
     * kMaxRows (or num, den are 0). Rows past the end move too, as far as
     * they fit.
     */
    bool scaleRows(uint32_t index, uint32_t num, uint32_t den) {
        if (index >= numPatterns || num == 0 || den == 0 || num > config::kMaxRows || den > config::kMaxRows) {
            return false;
        }
        Pattern& p = patterns[index];
        const uint32_t rows = (p.numRows * num + den - 1) / den;
        if (rows < 1 || rows > config::kMaxRows) return false;
        // In place: rows move away from row 0 when stretching, so go from the
        // end; towards it when squashing, so go from the start.
        auto move = [&](uint32_t r) {
            for (uint32_t c = 0; c < config::kMaxColumns; ++c) {
                const Cell cell = p.cells[r][c];
                p.cells[r][c] = Cell();
                if ((r * num) % den == 0 && r * num / den < config::kMaxRows) p.cells[r * num / den][c] = cell;
            }
        };
        if (num >= den) {
            for (uint32_t r = config::kMaxRows; r-- > 0;) move(r);
        } else {
            for (uint32_t r = 0; r < config::kMaxRows; ++r) move(r);
        }
        p.numRows = static_cast<uint16_t>(rows);
        return true;
    }

    /**
     * Give `channel` `count` columns (1..kMaxColumns), in all its patterns.
     * Columns beyond the count keep their cells and instruments, so a removed
     * column comes back as it was. An added column with no instrument takes
     * the one before it.
     */
    bool setColumns(uint32_t channel, uint32_t count) {
        if (channel >= numChannels || count < 1 || count > config::kMaxColumns) return false;
        ChannelSetup& c = channels[channel];
        for (uint32_t col = c.numColumns; col < count; ++col) {
            if (c.instruments[col] == kNoIndex && col > 0) c.instruments[col] = c.instruments[col - 1];
        }
        c.numColumns = static_cast<uint8_t>(count);
        return true;
    }

    /**
     * Remove `channel`'s `column` from all its patterns: later columns move
     * left (with their instruments), and the removed one moves just past the
     * end, where adding a column brings it back. A channel keeps at least one column.
     */
    bool removeColumn(uint32_t channel, uint32_t column) {
        if (channel >= numChannels) return false;
        ChannelSetup& c = channels[channel];
        if (c.numColumns <= 1 || column >= c.numColumns) return false;
        const uint32_t last = c.numColumns - 1u;
        auto rotate = [&](auto* items) {
            const auto removed = items[column];
            for (uint32_t col = column; col < last; ++col) items[col] = items[col + 1];
            items[last] = removed;
        };
        rotate(c.instruments);
        for (uint32_t i = 0; i < numPatterns; ++i) {
            if (patterns[i].channel != channel) continue;
            for (auto& row : patterns[i].cells) rotate(row);
        }
        c.numColumns = static_cast<uint8_t>(last);
        return true;
    }

    /// True when every entry of `s` names a playable pattern.
    bool isPlayable(const Sequence& s) const {
        if (s.length == 0) return false;
        for (uint8_t i = 0; i < s.length; ++i) {
            if (s.entries[i] >= numPatterns || !patterns[s.entries[i]].isValid()) return false;
        }
        return true;
    }
    /// Pattern `index`, or nullptr if there is no such playable pattern.
    const Pattern* pattern(uint32_t index) const {
        return index < numPatterns && patterns[index].isValid() ? &patterns[index] : nullptr;
    }

    bool addMapping(const Mapping& m) {
        if (numMappings >= config::kMaxMappings) return false;
        mappings[numMappings++] = m;
        return true;
    }
};

static_assert(config::kMaxChannels <= 32, "layer bitmasks are 32 bits");

} // namespace adaptive
