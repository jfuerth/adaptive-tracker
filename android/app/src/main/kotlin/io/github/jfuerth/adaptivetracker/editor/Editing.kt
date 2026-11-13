package io.github.jfuerth.adaptivetracker.editor

import io.github.jfuerth.adaptivetracker.instrument.InstrumentParam
import java.util.Locale
import kotlin.math.log2
import kotlin.math.roundToInt

/**
 * Where the cursor is in a cell: the note, or one hex digit. Typing a digit
 * moves to the next digit of the same group (volume: 2 digits, effect: command
 * plus 2 parameter digits).
 */
enum class Field {
    NOTE, VOLUME_HI, VOLUME_LO, EFFECT, PARAM_X, PARAM_Y;

    val isVolume get() = this == VOLUME_HI || this == VOLUME_LO
    val isEffect get() = this == EFFECT || this == PARAM_X || this == PARAM_Y

    /** The next digit in this field's group, or null at the group's last digit. */
    val nextDigit: Field?
        get() = when (this) {
            VOLUME_HI -> VOLUME_LO
            EFFECT -> PARAM_X
            PARAM_X -> PARAM_Y
            else -> null
        }
}

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

    /** Write hex [digit] (0-15) at [field]. Volume is clamped to 7F. */
    fun typeHex(cell: CellData, field: Field, digit: Int): CellData {
        val d = digit and 0xF
        val volume = if (cell.volume == CellData.VOLUME_DEFAULT) 0 else cell.volume
        return when (field) {
            Field.VOLUME_HI -> cell.copy(volume = ((d shl 4) or (volume and 0x0F)).coerceAtMost(0x7F))
            Field.VOLUME_LO -> cell.copy(volume = (volume and 0xF0) or d)
            Field.EFFECT -> cell.copy(effect = CellData.effectFromDigit(d))
            Field.PARAM_X -> cell.copy(param = (d shl 4) or (cell.param and 0x0F))
            Field.PARAM_Y -> cell.copy(param = (cell.param and 0xF0) or d)
            Field.NOTE -> cell
        }
    }

    /** Clear one field of a cell. */
    fun clear(cell: CellData, field: Field): CellData = when (field) {
        Field.NOTE -> cell.copy(note = CellData.NOTE_EMPTY, accidental = 0)
        Field.VOLUME_HI, Field.VOLUME_LO -> cell.copy(volume = CellData.VOLUME_DEFAULT)
        Field.EFFECT, Field.PARAM_X, Field.PARAM_Y -> cell.copy(effect = CellData.EFFECT_NONE, param = 0)
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

    /** Three characters: command and parameter. Parameter digits typed before a command show after a dot. */
    fun effect(cell: CellData): String = when {
        cell.effect != CellData.EFFECT_NONE -> "${cell.effect.toChar()}%02X".format(cell.param)
        cell.param != 0 -> ".%02X".format(cell.param)
        else -> "..."
    }
}

/**
 * The live description line: what an effect (as typed so far) will do.
 * [effectValue] gives an effect parameter in natural units as the engine maps
 * it (NativeAudio.effectValue: Hz for F, Q for 5, 0..1 for 6), so the mapping
 * lives in one place.
 */
object EffectDescription {
    fun describe(
        cell: CellData,
        mode: PitchMode,
        eventNames: List<String> = emptyList(),
        effectValue: (effect: Int, param: Int) -> Float = { _, _ -> Float.NaN },
        ticksPerBeat: Int = 0,
    ): String {
        if (cell.effect == CellData.EFFECT_NONE) return "No effect · type a command digit"
        val p = cell.param
        val x = p shr 4
        val y = p and 0xF
        val code = "${cell.effect.toChar()}%02X".format(p)
        val units = if (mode == PitchMode.DEGREE) "scale steps" else "semitones"
        val text = when (cell.effect.toChar()) {
            '0' -> if (p == 0) "Arpeggio · 000 does nothing" else "Arpeggio · note, +$x, +$y $units"
            '1' -> if (p == 0) "Slide · 100 stops the slide" else "Slide up · $p/16 semitone per tick, until stopped"
            '2' -> if (p == 0) "Slide · 200 stops the slide" else "Slide down · $p/16 semitone per tick, until stopped"
            '3' -> "Glide to note · " + if (p == 0) "300 reuses the last speed" else "$p/16 semitone per tick"
            '4' -> if (p == 0) "Vibrato off" else
                "Vibrato · rate %.1f Hz, depth %.1f semitone".format(Locale.US, 0.5 + x * 0.5, y * 0.1)
            '5' -> "Filter resonance · " + value(effectValue, cell) { "Q %.1f".format(Locale.US, it) }
            '6' -> "Waveform shape · " + value(effectValue, cell) { "${(it * 100).roundToInt()}%" }
            '8' -> if (p == 0) "Filter sweep · 800 stops the sweep" else "Filter sweep up · " + sweep(p, effectValue, ticksPerBeat)
            '9' -> if (p == 0) "Filter sweep · 900 stops the sweep" else "Filter sweep down · " + sweep(p, effectValue, ticksPerBeat)
            'A' -> when {
                x != 0 -> "Volume slide up · $x/64 per tick"
                y != 0 -> "Volume slide down · $y/64 per tick"
                else -> "Volume slide · A00 reuses the last slide"
            }
            'B' -> "Fire event $p" + (eventNames.getOrNull(p)?.let { " · $it" } ?: " · no such event")
            'C' -> "Set volume · ${p.coerceAtMost(0x7F)}/127"
            'F' -> "Filter cutoff · " + value(effectValue, cell) { InstrumentParam.hz(it) }
            else -> "Reserved · no effect"
        }
        return "$code · $text"
    }

    /**
     * 8xx/9xx move the cutoff by xx/16 Fxx steps per effect tick. Octaves per
     * Fxx step come from the engine's Fxx mapping, and effect ticks per beat
     * from the engine's clock ([ticksPerBeat], 0 if unknown).
     */
    private fun sweep(p: Int, effectValue: (Int, Int) -> Float, ticksPerBeat: Int): String {
        val lowest = effectValue('F'.code, 0)
        val highest = effectValue('F'.code, 255)
        if (lowest.isNaN() || highest.isNaN() || ticksPerBeat <= 0) return "$p/16 cutoff step per tick, until stopped"
        val octavesPerStep = log2(highest / lowest) / 255.0
        return "%.2f octave per beat, until stopped".format(Locale.US, p / 16.0 * ticksPerBeat * octavesPerStep)
    }

    /** The parameter in natural units, or its raw hex if the engine isn't available. */
    private fun value(effectValue: (Int, Int) -> Float, cell: CellData, format: (Float) -> String): String {
        val v = effectValue(cell.effect, cell.param)
        return if (v.isNaN()) "%02X".format(cell.param) else format(v)
    }
}
