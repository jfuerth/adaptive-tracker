package io.github.jfuerth.adaptivetracker.editor

/** A key: root pitch class 0-11 and a 12-bit scale mask (see adaptive::scales). */
data class Key(val root: Int, val scale: Int)

/** What the editor needs to turn cells into pitches and back. */
data class PitchContext(val home: Key, val current: Key, val transpose: Int = 0)

/**
 * Pitch maths, ported from core/engine/include/adaptive/pitch.hpp (Wrap degree
 * mapping, the engine's default). Keep the two in step.
 */
object Pitch {
    const val MAJOR = 0b1010_1011_0101  // 0, 2, 4, 5, 7, 9, 11

    fun scaleSize(scale: Int): Int = Integer.bitCount(scale and 0xFFF)

    fun inScale(scale: Int, pitchClass: Int): Boolean = (scale shr Math.floorMod(pitchClass, 12)) and 1 == 1

    /** Semitones above the root of the scale's [index]-th note. */
    fun scaleNoteSemitones(scale: Int, index: Int): Int {
        var remaining = index
        for (i in 0 until 12) {
            if ((scale shr i) and 1 == 1) {
                if (remaining == 0) return i
                remaining--
            }
        }
        return 0
    }

    /** Shortest signed distance between roots, -5..+6. */
    fun rootShift(from: Int, to: Int): Int {
        val d = Math.floorMod(to - from, 12)
        return if (d > 6) d - 12 else d
    }

    fun resolveDegree(octave: Int, degree: Int, accidental: Int, ctx: PitchContext): Int {
        val base = 12 * (octave + 1) + ctx.home.root + rootShift(ctx.home.root, ctx.current.root) + ctx.transpose
        val size = scaleSize(ctx.current.scale).coerceAtLeast(1)
        val q = Math.floorDiv(degree, size)
        val semitones = scaleNoteSemitones(ctx.current.scale, degree - q * size) + 12 * q
        return (base + semitones + accidental).coerceIn(0, 127)
    }

    /** MIDI pitch of a cell's note, read according to [mode]. */
    fun resolve(mode: PitchMode, note: Int, accidental: Int, ctx: PitchContext): Int = when (mode) {
        PitchMode.ABSOLUTE -> note
        PitchMode.TRANSPOSE -> (note + rootShift(ctx.home.root, ctx.current.root) + ctx.transpose).coerceIn(0, 127)
        PitchMode.DEGREE -> resolveDegree(note / 12, note % 12, accidental, ctx)
    }

    /**
     * The cell note (and accidental) that sounds as MIDI pitch [midi] in the
     * current key: what you press is what you hear. In Degree mode, notes outside
     * the scale get an accidental (sharp preferred). Returns null if impossible.
     */
    fun encode(midi: Int, mode: PitchMode, ctx: PitchContext): Pair<Int, Int>? = when (mode) {
        PitchMode.ABSOLUTE -> midi to 0
        PitchMode.TRANSPOSE ->
            (midi - rootShift(ctx.home.root, ctx.current.root) - ctx.transpose).takeIf { it in 0..127 }?.let { it to 0 }
        PitchMode.DEGREE -> encodeDegree(midi, ctx)
    }

    private fun encodeDegree(midi: Int, ctx: PitchContext): Pair<Int, Int>? {
        val size = scaleSize(ctx.current.scale)
        for (accidental in listOf(0, 1, -1)) {
            for (octave in 0..10) {
                for (degree in 0 until size) {
                    val note = octave * 12 + degree
                    if (note > 127) continue
                    if (resolveDegree(octave, degree, accidental, ctx) == midi) return note to accidental
                }
            }
        }
        return null
    }

    private val NAMES = listOf("C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-")

    /** Tracker-style note name, e.g. "C-4", "F#3" (middle C = C-4). */
    fun noteName(midi: Int): String {
        val octave = midi / 12 - 1
        return NAMES[midi % 12] + if (octave < 0) "-" else octave.toString()
    }

    /** Key-label names for the keyboard (12 pitch classes). */
    val KEY_NAMES = listOf("C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B")
}
