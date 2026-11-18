#pragma once

#include <adaptive/config.hpp>

#include <cstdint>

namespace adaptive {

/**
 * Effect commands: one hex digit each, stored as the ASCII character a tracker
 * displays ('0'-'9', 'A'-'F'). 0 (not '0') means "no effect".
 * See DESIGN.md (Effects) for units and timing.
 */
namespace fx {
inline constexpr uint8_t kNone = 0;
inline constexpr uint8_t kArpeggio = '0';         ///< 0xy: cycle note, +x, +y (scale steps in degree mode)
inline constexpr uint8_t kPortaUp = '1';          ///< 1xx: pitch up xx/16 semitone per effect tick (persists; 100 stops)
inline constexpr uint8_t kPortaDown = '2';        ///< 2xx: pitch down xx/16 semitone per effect tick (persists; 200 stops)
inline constexpr uint8_t kTonePorta = '3';        ///< 3xx: glide to this row's note at xx/16 semitone per effect tick, across rows (300 reuses the speed)
inline constexpr uint8_t kVibrato = '4';          ///< 4xy: vibrato rate x, depth y (persists; 400 = off)
inline constexpr uint8_t kFilterResonance = '5';  ///< 5xx: filter Q across the synth's full range
inline constexpr uint8_t kWaveShape = '6';        ///< 6xx: waveform shape 0..1
inline constexpr uint8_t kFilterSweepUp = '8';    ///< 8xx: cutoff rises xx/16 Fxx steps per effect tick (persists; 800 stops)
inline constexpr uint8_t kFilterSweepDown = '9';  ///< 9xx: cutoff falls xx/16 Fxx steps per effect tick (persists; 900 stops)
inline constexpr uint8_t kVolumeSlide = 'A';      ///< Axy: volume up x or down y, 1/64 per effect tick
inline constexpr uint8_t kFireEvent = 'B';        ///< Bxx: fire event xx
inline constexpr uint8_t kSetVolume = 'C';        ///< Cxx: volume 00..7F
inline constexpr uint8_t kRepeat = 'D';           ///< Dxy: replay the note every y effect ticks, each x/16 quieter (persists; D00 stops)
inline constexpr uint8_t kExtended = 'E';         ///< Exy: x picks the command (below), y its value
inline constexpr uint8_t kExtStaccato = 0xC;      ///< ECy: release the note y effect ticks into the row
inline constexpr uint8_t kExtDelay = 0xD;         ///< EDy: the row's note, note-off and volume wait y effect ticks
inline constexpr uint8_t kFilterCutoff = 'F';     ///< Fxx: base cutoff, 20 Hz..20 kHz exponential

/// The command character for a hex digit 0-15.
constexpr uint8_t fromDigit(int digit) {
    return static_cast<uint8_t>(digit < 10 ? '0' + digit : 'A' + (digit - 10));
}
/// The hex digit 0-15 for a command character, or -1 for kNone or anything else.
constexpr int toDigit(uint8_t command) {
    if (command >= '0' && command <= '9') return command - '0';
    if (command >= 'A' && command <= 'F') return command - 'A' + 10;
    return -1;
}
} // namespace fx

/**
 * One cell: a single row of a single column.
 *
 * How `note` is read depends on the channel's PitchMode (see pitch.hpp):
 * - Absolute and Transpose: a MIDI note number 0-127.
 * - Degree: octave * 12 + scale-degree index (degree 0 = the key's root), plus
 *   `accidental` in semitones (-1 flat, +1 sharp) relative to the scale note.
 */
struct Cell {
    static constexpr uint8_t kNoteEmpty = 0xFF;  ///< Nothing happens on this row.
    static constexpr uint8_t kNoteOff = 0xFE;    ///< Release the column's voice.
    static constexpr uint8_t kVolumeDefault = 0xFF;

    uint8_t note = kNoteEmpty;        ///< See above, or kNoteEmpty / kNoteOff.
    int8_t accidental = 0;            ///< Degree mode only.
    uint8_t volume = kVolumeDefault;  ///< 0-127, or kVolumeDefault (no change).
    uint8_t effect = fx::kNone;
    uint8_t effectParam = 0;

    /// A note in an Absolute or Transpose channel.
    static constexpr Cell on(uint8_t midiNote, uint8_t vol = kVolumeDefault) {
        return Cell{midiNote, 0, vol, fx::kNone, 0};
    }
    /// A note in a Degree channel. Octave 4, degree 0 is the key's root in the
    /// octave of middle C.
    static constexpr Cell degree(uint8_t octave, uint8_t degreeIndex, int8_t accidental = 0,
                                 uint8_t vol = kVolumeDefault) {
        return Cell{static_cast<uint8_t>(octave * 12 + degreeIndex), accidental, vol, fx::kNone, 0};
    }
    static constexpr Cell off() { return Cell{kNoteOff, 0, kVolumeDefault, fx::kNone, 0}; }
    /// An effect with no note.
    static constexpr Cell effectOnly(uint8_t effect, uint8_t param) {
        return Cell{kNoteEmpty, 0, kVolumeDefault, effect, param};
    }

    /// Returns a copy of this cell with an effect added.
    constexpr Cell with(uint8_t eff, uint8_t param) const {
        Cell c = *this;
        c.effect = eff;
        c.effectParam = param;
        return c;
    }

    constexpr bool isNote() const { return note <= 127; }
    constexpr bool isOff() const { return note == kNoteOff; }
};

/**
 * A block of rows for one channel. Each pattern sets its own length and speed,
 * so channels can loop at different lengths and subdivisions.
 */
struct Pattern {
    uint16_t numRows = 16;
    /// Speed: effect ticks per row, as in trackers (24 per beat, so 6 = four
    /// rows per beat). Rows needn't line up with beats (e.g. 5).
    uint8_t ticksPerRow = 6;
    /// The channel the pattern belongs to: its pitch mode, columns and instruments.
    uint8_t channel = 0;
    Cell cells[config::kMaxRows][config::kMaxColumns] = {};

    constexpr uint32_t engineTicksPerRow() const { return uint32_t(ticksPerRow) * config::kTicksPerEffectTick; }
    constexpr uint32_t lengthTicks() const { return numRows * engineTicksPerRow(); }

    /// True when the length and speed are usable by the engine.
    constexpr bool isValid() const { return numRows > 0 && numRows <= config::kMaxRows && ticksPerRow > 0; }
};

/**
 * The patterns a channel plays, in order, looping back to the first after the
 * last. Entries are indices into the song's patterns.
 */
struct Sequence {
    uint8_t entries[config::kMaxSequenceLength] = {};
    uint8_t length = 0;

    /// Append a pattern. Returns false if the sequence is full.
    bool append(uint8_t pattern) {
        if (length >= config::kMaxSequenceLength) return false;
        entries[length++] = pattern;
        return true;
    }
};

/// Changes to a sequence (Command::editSequence, Song::editSequence).
enum class SeqEdit : uint8_t {
    Insert,  ///< put a pattern before entry `pos` (pos == length appends)
    Remove,  ///< take out entry `pos`
    Set,     ///< entry `pos` plays another pattern
};

static_assert(config::kMaxSequenceLength <= 255 && config::kMaxPatterns <= 255, "indices are 8 bits");

} // namespace adaptive
