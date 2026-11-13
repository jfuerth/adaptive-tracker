package io.github.jfuerth.adaptivetracker.editor

/** Mirrors adaptive::PitchMode; ordinals match the C++ values. */
enum class PitchMode { ABSOLUTE, TRANSPOSE, DEGREE }

/**
 * One pattern cell, mirroring adaptive::Cell. See pattern.hpp.
 *
 * For Absolute and Transpose channels [note] is a MIDI note. For Degree channels
 * it is octave * 12 + scale-degree index, with [accidental] in semitones.
 * [effect] is the ASCII command character ('0'-'9', 'A'-'F') or [EFFECT_NONE].
 */
data class CellData(
    val note: Int = NOTE_EMPTY,
    val accidental: Int = 0,
    val volume: Int = VOLUME_DEFAULT,
    val effect: Int = EFFECT_NONE,
    val param: Int = 0,
) {
    val isNote: Boolean get() = note in 0..127
    val isOff: Boolean get() = note == NOTE_OFF

    companion object {
        const val NOTE_EMPTY = 0xFF
        const val NOTE_OFF = 0xFE
        const val VOLUME_DEFAULT = 0xFF
        const val EFFECT_NONE = 0

        /** The command character for a hex digit 0-15. */
        fun effectFromDigit(digit: Int): Int = if (digit < 10) '0'.code + digit else 'A'.code + digit - 10

        /** The hex digit for a command character, or -1. */
        fun digitFromEffect(effect: Int): Int = when (effect) {
            in '0'.code..'9'.code -> effect - '0'.code
            in 'A'.code..'F'.code -> effect - 'A'.code + 10
            else -> -1
        }

        /** Decodes the 5-bytes-per-cell layout returned by NativeAudio.readPattern. */
        fun fromBytes(bytes: ByteArray, offset: Int) = CellData(
            note = bytes[offset].toInt() and 0xFF,
            accidental = bytes[offset + 1].toInt(),  // signed
            volume = bytes[offset + 2].toInt() and 0xFF,
            effect = bytes[offset + 3].toInt() and 0xFF,
            param = bytes[offset + 4].toInt() and 0xFF,
        )
    }
}
