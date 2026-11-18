package io.github.jfuerth.adaptivetracker.editor

/** One value an effect takes: a whole byte (xx), or one hex digit. */
data class EffectValue(val label: String, val max: Int)

/**
 * One effect as the effect reference offers it: its command, and for the
 * extended `E` command the sub-command in x. Some commands are offered as
 * two effects (`A` slides volume up with x, down with y).
 */
data class EffectKind(
    val group: String,
    val name: String,
    val command: Char,
    val values: List<EffectValue>,
    /** `E` commands: the sub-command, the parameter's x digit. */
    val sub: Int? = null,
    /** Packs the values into the parameter byte. */
    private val pack: (List<Int>) -> Int,
    /** The values in a parameter byte. */
    private val unpack: (Int) -> List<Int>,
) {
    /** What the cell's command column shows: "3", or "EC" for extended commands. */
    val code: String get() = sub?.let { "$command${"%X".format(it)}" } ?: command.toString()

    /** [cell] with this effect and [values] (clamped to each value's range). */
    fun apply(cell: CellData, values: List<Int>): CellData {
        val v = this.values.mapIndexed { i, spec -> values.getOrElse(i) { 0 }.coerceIn(0, spec.max) }
        return cell.copy(effect = command.code, param = pack(v) and 0xFF)
    }

    /** This effect's values in [param]. */
    fun valuesOf(param: Int): List<Int> = unpack(param and 0xFF)

    companion object {
        private fun byte(label: String, max: Int = 0xFF) = EffectValue(label, max)
        private fun digit(label: String) = EffectValue(label, 0xF)

        private fun whole(group: String, name: String, command: Char, label: String, max: Int = 0xFF) =
            EffectKind(group, name, command, listOf(byte(label, max)), pack = { it[0] }, unpack = { listOf(it) })

        private fun pair(group: String, name: String, command: Char, x: String, y: String) =
            EffectKind(group, name, command, listOf(digit(x), digit(y)),
                pack = { (it[0] shl 4) or it[1] }, unpack = { listOf(it shr 4, it and 0xF) })

        private fun extended(group: String, name: String, sub: Int, label: String) =
            EffectKind(group, name, 'E', listOf(digit(label)), sub = sub,
                pack = { (sub shl 4) or it[0] }, unpack = { listOf(it and 0xF) })

        /** Every effect, grouped by what it changes, in the reference's order. */
        val all: List<EffectKind> = listOf(
            pair("Pitch", "Arpeggio", '0', "Second note (steps up)", "Third note (steps up)"),
            whole("Pitch", "Slide up", '1', "Speed (16ths of a semitone per tick)"),
            whole("Pitch", "Slide down", '2', "Speed (16ths of a semitone per tick)"),
            whole("Pitch", "Glide to note", '3', "Speed (16ths of a semitone per tick; 0 reuses the last)"),
            pair("Pitch", "Vibrato", '4', "Rate", "Depth (tenths of a semitone)"),
            whole("Volume", "Set volume", 'C', "Volume", max = 0x7F),
            EffectKind("Volume", "Volume slide up", 'A', listOf(digit("Speed (64ths per tick)")),
                pack = { it[0] shl 4 }, unpack = { listOf(it shr 4) }),
            EffectKind("Volume", "Volume slide down", 'A', listOf(digit("Speed (64ths per tick)")),
                pack = { it[0] }, unpack = { listOf(it and 0xF) }),
            extended("Timing", "Staccato", 0xC, "Release after (ticks)"),
            extended("Timing", "Delay", 0xD, "Start after (ticks)"),
            pair("Timing", "Repeat / echo", 'D', "Fade per repeat (16ths)", "Every (ticks; 0 stops)"),
            whole("Tone", "Filter cutoff", 'F', "Cutoff"),
            whole("Tone", "Filter sweep up", '8', "Speed (0 stops)"),
            whole("Tone", "Filter sweep down", '9', "Speed (0 stops)"),
            whole("Tone", "Filter resonance", '5', "Resonance"),
            whole("Tone", "Waveform shape", '6', "Shape (saw … triangle … square)"),
            whole("Events", "Fire event", 'B', "Event"),
        )

        /** The effect [cell] has, as the reference offers it, or null (none, or not one it knows). */
        fun of(cell: CellData): EffectKind? {
            val command = cell.effect.takeIf { it != CellData.EFFECT_NONE }?.toChar() ?: return null
            return when (command) {
                'A' -> all.first { it.command == 'A' && (it.name.endsWith("up") == (cell.param shr 4 != 0)) }
                'E' -> all.firstOrNull { it.command == 'E' && it.sub == cell.param shr 4 }
                else -> all.firstOrNull { it.command == command }
            }
        }
    }
}
