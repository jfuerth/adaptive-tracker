package io.github.jfuerth.adaptivetracker.editor

import java.util.Locale
import kotlin.math.pow

/** Which part of a cell the editor is writing. */
enum class Field { NOTE, VOLUME, EFFECT, PARAM }

/** Editing operations on cells. Pure functions, unit-tested on the JVM. */
object Editing {
    /**
     * The cell for a chord played on the keyboard: the lowest note, plus a 0xy
     * arpeggio for the next two notes (scale steps in Degree mode, semitones
     * otherwise; offsets must be 1-15). A single note keeps [existing]'s volume
     * and effect; a chord replaces the effect with the arpeggio.
     */
    fun chordCell(midis: Collection<Int>, mode: PitchMode, ctx: PitchContext, existing: CellData): CellData? {
        val notes = midis.distinct().sorted()
        if (notes.isEmpty()) return null
        val (note, accidental) = Pitch.encode(notes.first(), mode, ctx) ?: return null
        if (notes.size == 1) return existing.copy(note = note, accidental = accidental)

        val offsets = if (mode == PitchMode.DEGREE) {
            val size = Pitch.scaleSize(ctx.current.scale)
            fun index(midi: Int): Int? = Pitch.encode(midi, mode, ctx)?.let { (n, _) -> (n / 12) * size + n % 12 }
            val baseIndex = index(notes.first()) ?: return null
            notes.drop(1).mapNotNull { m -> index(m)?.let { it - baseIndex } }
        } else {
            notes.drop(1).map { it - notes.first() }
        }.filter { it in 1..15 }.distinct().take(2)

        if (offsets.isEmpty()) return existing.copy(note = note, accidental = accidental)
        val param = (offsets[0] shl 4) or (offsets.getOrElse(1) { 0 })
        return existing.copy(note = note, accidental = accidental, effect = '0'.code, param = param)
    }

    /** Number of hex digits a field takes. */
    fun digitsFor(field: Field): Int = when (field) {
        Field.VOLUME -> 2
        Field.EFFECT -> 3
        Field.PARAM -> 2
        Field.NOTE -> 0
    }

    /**
     * Type hex [digit] (0-15) at [position] of [field]. Returns the new cell and
     * the next position, or -1 when the field is complete.
     */
    fun typeHex(cell: CellData, field: Field, position: Int, digit: Int): Pair<CellData, Int> {
        val d = digit and 0xF
        val updated = when (field) {
            Field.VOLUME -> {
                val current = if (cell.volume == CellData.VOLUME_DEFAULT) 0 else cell.volume
                val v = if (position == 0) d shl 4 else (current and 0xF0) or d
                cell.copy(volume = v.coerceAtMost(0x7F))
            }
            Field.EFFECT -> when (position) {
                0 -> cell.copy(effect = CellData.effectFromDigit(d))
                1 -> cell.copy(param = (d shl 4) or (cell.param and 0x0F))
                else -> cell.copy(param = (cell.param and 0xF0) or d)
            }
            Field.PARAM -> when (position) {
                0 -> cell.copy(param = (d shl 4) or (cell.param and 0x0F))
                else -> cell.copy(param = (cell.param and 0xF0) or d)
            }
            Field.NOTE -> cell
        }
        val next = position + 1
        return updated to if (next >= digitsFor(field)) -1 else next
    }

    /** Clear one field of a cell. */
    fun clear(cell: CellData, field: Field): CellData = when (field) {
        Field.NOTE -> cell.copy(note = CellData.NOTE_EMPTY, accidental = 0)
        Field.VOLUME -> cell.copy(volume = CellData.VOLUME_DEFAULT)
        Field.EFFECT, Field.PARAM -> cell.copy(effect = CellData.EFFECT_NONE, param = 0)
    }
}

/** How cells are shown in the grid. */
object CellFormat {
    fun note(cell: CellData, mode: PitchMode, ctx: PitchContext): String = when {
        cell.isOff -> "==="
        !cell.isNote -> "---"
        else -> Pitch.noteName(Pitch.resolve(mode, cell.note, cell.accidental, ctx))
    }

    fun volume(cell: CellData): String =
        if (cell.volume == CellData.VOLUME_DEFAULT) ".." else "%02X".format(cell.volume)

    fun effect(cell: CellData): String =
        if (cell.effect == CellData.EFFECT_NONE) "..." else "${cell.effect.toChar()}%02X".format(cell.param)
}

/** The live description line: what an effect (as typed so far) will do. */
object EffectDescription {
    fun describe(cell: CellData, mode: PitchMode, eventNames: List<String> = emptyList()): String {
        if (cell.effect == CellData.EFFECT_NONE) return "No effect · type a command digit"
        val p = cell.param
        val x = p shr 4
        val y = p and 0xF
        val code = "${cell.effect.toChar()}%02X".format(p)
        val units = if (mode == PitchMode.DEGREE) "scale steps" else "semitones"
        val text = when (cell.effect.toChar()) {
            '0' -> if (p == 0) "Arpeggio · 000 does nothing" else "Arpeggio · note, +$x, +$y $units"
            '1' -> "Slide up · " + speed(p)
            '2' -> "Slide down · " + speed(p)
            '3' -> "Slide to note · " + speed(p)
            '4' -> if (p == 0) "Vibrato off" else
                "Vibrato · rate %.1f Hz, depth %.1f semitone".format(Locale.US, 0.5 + x * 0.5, y * 0.1)
            '5' -> "Filter resonance · Q %.1f".format(Locale.US, 0.1 + 19.9 * (p / 255.0).pow(2))
            '6' -> "Waveform shape · ${p * 100 / 255}%"
            'A' -> when {
                x != 0 -> "Volume slide up · $x/64 per tick"
                y != 0 -> "Volume slide down · $y/64 per tick"
                else -> "Volume slide · A00 reuses the last slide"
            }
            'B' -> "Fire event $p" + (eventNames.getOrNull(p)?.let { " · $it" } ?: " · no such event")
            'C' -> "Set volume · ${p.coerceAtMost(0x7F)}/127"
            'F' -> "Filter cutoff · ${cutoffHz(p)}"
            else -> "Reserved · no effect"
        }
        return "$code · $text"
    }

    private fun speed(p: Int) = if (p == 0) "00 reuses the last speed" else "$p/16 semitone per tick"

    private fun cutoffHz(p: Int): String {
        val hz = 20.0 * 2.0.pow(9.965784 * p / 255.0)
        return if (hz >= 1000) "%.1f kHz".format(Locale.US, hz / 1000) else "${hz.toInt()} Hz"
    }
}
