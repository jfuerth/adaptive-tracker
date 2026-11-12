#pragma once

#include <adaptive/config.hpp>

#include <cstdint>

namespace adaptive {

/**
 * Effect commands. Values are the ASCII characters a tracker would display.
 * 0 means "no effect". See DESIGN.md (Effects) for units and timing.
 */
namespace fx {
inline constexpr uint8_t kNone = 0;
inline constexpr uint8_t kArpeggio = '0';         ///< 0xy: cycle note, +x, +y (scale steps in degree mode)
inline constexpr uint8_t kPortaUp = '1';          ///< 1xx: pitch up xx/16 semitone per effect tick
inline constexpr uint8_t kPortaDown = '2';        ///< 2xx: pitch down xx/16 semitone per effect tick
inline constexpr uint8_t kTonePorta = '3';        ///< 3xx: slide to this row's note at xx/16 semitone per effect tick
inline constexpr uint8_t kVolumeSlide = 'A';      ///< Axy: volume up x or down y, 1/64 per effect tick
inline constexpr uint8_t kFilterCutoff = 'F';     ///< Fxx: base cutoff, 20 Hz..20 kHz exponential
inline constexpr uint8_t kFilterResonance = 'Q';  ///< Qxx: filter Q across the synth's full range
inline constexpr uint8_t kWaveShape = 'W';        ///< Wxx: waveform shape 0..1
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

/// True when a pattern can use this many rows per beat: it must divide the
/// engine's tick resolution so every row starts exactly on a tick.
constexpr bool isValidRowsPerBeat(uint32_t rowsPerBeat) {
    return rowsPerBeat > 0 && rowsPerBeat <= config::kTicksPerBeat &&
           config::kTicksPerBeat % rowsPerBeat == 0;
}

/**
 * A block of rows for one channel. Each pattern sets its own length and rows per
 * beat, so channels can loop at different lengths and subdivisions.
 */
struct Pattern {
    uint16_t numRows = 16;
    uint8_t rowsPerBeat = 4;
    Cell cells[config::kMaxRows][config::kMaxColumns] = {};

    constexpr uint32_t ticksPerRow() const { return config::kTicksPerBeat / rowsPerBeat; }
    constexpr uint32_t lengthTicks() const { return numRows * ticksPerRow(); }

    /// True when the length and rows per beat are usable by the engine.
    constexpr bool isValid() const {
        return numRows > 0 && numRows <= config::kMaxRows && isValidRowsPerBeat(rowsPerBeat);
    }
};

/**
 * The order in which a channel plays patterns. Loops back to the first entry
 * after the last. The patterns must outlive the sequence's use by the engine.
 */
struct Sequence {
    const Pattern* entries[config::kMaxSequenceLength] = {};
    uint16_t length = 0;

    /// Append a pattern. Returns false if the sequence is full or the pattern invalid.
    bool append(const Pattern* p) {
        if (length >= config::kMaxSequenceLength || p == nullptr || !p->isValid()) return false;
        entries[length++] = p;
        return true;
    }
};

} // namespace adaptive
