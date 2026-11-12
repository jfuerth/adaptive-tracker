#pragma once

#include <adaptive/config.hpp>

#include <cstdint>

namespace adaptive {

/**
 * One cell: a single row of a single column.
 *
 * Notes are absolute MIDI note numbers for now. The scale-degree representation
 * (see DESIGN.md, key change) will be added as an alternative encoding; effect
 * fields come with the milestone 2 sequencer.
 */
struct Cell {
    static constexpr uint8_t kNoteEmpty = 0xFF;  ///< Nothing happens on this row.
    static constexpr uint8_t kNoteOff = 0xFE;    ///< Release the column's voice.
    static constexpr uint8_t kVolumeDefault = 0xFF;

    uint8_t note = kNoteEmpty;           ///< 0-127 MIDI note, or kNoteEmpty / kNoteOff.
    uint8_t volume = kVolumeDefault;     ///< 0-127, or kVolumeDefault.

    static constexpr Cell on(uint8_t midiNote, uint8_t vol = kVolumeDefault) { return Cell{midiNote, vol}; }
    static constexpr Cell off() { return Cell{kNoteOff, kVolumeDefault}; }

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

} // namespace adaptive
