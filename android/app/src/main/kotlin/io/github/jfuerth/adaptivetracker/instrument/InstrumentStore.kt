package io.github.jfuerth.adaptivetracker.instrument

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshots.SnapshotStateList
import androidx.compose.runtime.toMutableStateList
import io.github.jfuerth.adaptivetracker.NativeAudio
import io.github.jfuerth.adaptivetracker.Song
import io.github.jfuerth.adaptivetracker.editor.PatternStore
import io.github.jfuerth.adaptivetracker.editor.PitchMode

/**
 * The UI's copy of each instrument. Each one is read from the engine the
 * first time it's shown; after that, edits update this copy and are sent to
 * the engine, which keeps the song (and saves it).
 */
object InstrumentStore {
    /** Each instrument's name, by index. */
    val names: SnapshotStateList<String> = mutableStateListOf()

    /** Forgets everything read from the engine: a different song has loaded. */
    fun reset() {
        loaded.clear()
        names.clear()
        names.addAll(List(NativeAudio.getInstrumentCount()) { i ->
            NativeAudio.getInstrumentName(i).ifEmpty { "Instrument ${i + 1}" }
        })
        assignments++
    }

    val capacity: Int by lazy { NativeAudio.getMaxInstruments() }

    /** Bumped whenever a column is given another instrument, for views that show assignments. */
    var assignments by mutableIntStateOf(0)
        private set

    /** The parameters the engine has and the editor knows, with the engine's ranges. */
    val specs: Map<InstrumentParam, ParamSpec> by lazy {
        ParamSpec.fromEngine(NativeAudio.getInstrumentParamKeys().toList(), NativeAudio.getInstrumentParamRanges())
    }

    private val loaded = HashMap<Int, SnapshotStateList<Float>>()

    /** Instrument [index]'s values in the engine's parameter order, or null if it can't be read. */
    fun values(index: Int): SnapshotStateList<Float>? = loaded[index] ?: NativeAudio.readInstrument(index)
        ?.toList()?.toMutableStateList()
        ?.also { loaded[index] = it }

    fun set(index: Int, param: InstrumentParam, value: Float) {
        val spec = specs[param] ?: return
        val values = values(index) ?: return
        val v = spec.clamp(value)
        if (values[spec.index] == v) return
        values[spec.index] = v
        NativeAudio.setInstrumentParam(index, spec.index, v)
    }

    val isFull: Boolean get() = names.size >= capacity

    /**
     * Adds an instrument: a copy of [copyOf] (named "<name> copy"), or the
     * synth's defaults. Returns its index, or null if the song is full.
     */
    fun create(copyOf: Int? = null): Int? {
        val source = copyOf?.let { values(it) }?.toFloatArray()
        val name = if (copyOf != null) uniqueName("${names[copyOf]} copy") else uniqueName("Instrument ${names.size + 1}")
        val index = NativeAudio.addInstrument(source, name)
        if (index < 0) return null
        names += name
        return index
    }

    fun rename(index: Int, name: String) {
        val trimmed = name.trim()
        if (index in names.indices && trimmed.isNotEmpty() && NativeAudio.renameInstrument(index, trimmed)) {
            names[index] = trimmed
        }
    }

    private fun uniqueName(base: String): String {
        if (base !in names) return base
        return generateSequence(2) { it + 1 }.map { "$base $it" }.first { it !in names }
    }

    /** Columns were added or removed: views showing assignments should look again. */
    fun columnsChanged() {
        assignments++
    }

    /**
     * [channel]'s [column] plays instrument [index] from now on. If the
     * instrument suggests a pitch mode, the column switches to it (its notes
     * are rewritten to sound the same).
     */
    fun assign(channel: Int, column: Int, index: Int) = Song.step("Assign instrument") {
        if (!NativeAudio.assignInstrument(channel, column, index)) return@step
        assignments++
        val hint = pitchHint(index) ?: return@step
        val current = NativeAudio.getColumnSettings(channel).getOrNull(column * 3)
        if (current != hint.ordinal) PatternStore.setColumnPitch(channel, column, hint)
    }

    /** The pitch mode columns switch to when given instrument [index], or null. */
    fun pitchHint(index: Int): PitchMode? = NativeAudio.getInstrumentPitch(index).takeIf { it >= 0 }?.let { PitchMode.entries[it] }

    fun setPitchHint(index: Int, mode: PitchMode?) {
        if (NativeAudio.setInstrumentPitch(index, mode?.ordinal ?: -1)) hints++
    }

    /** Goes up when a pitch hint changes. */
    var hints by mutableIntStateOf(0)
        private set

    /** Where a channel's columns are, and which instrument each plays (-1: none). */
    data class Slot(val channel: Int, val column: Int, val channelName: String, val columns: Int, val instrument: Int) {
        val label: String get() = if (columns == 1) channelName else "$channelName col ${column + 1}"
    }

    /** Every column of every channel. */
    fun slots(): List<Slot> = (0 until NativeAudio.getChannelCount()).flatMap { ch ->
        val name = NativeAudio.getChannelName(ch)
        val count = NativeAudio.getChannelColumns(ch)
        (0 until count).map { col -> Slot(ch, col, name, count, NativeAudio.getColumnInstrument(ch, col)) }
    }

    /**
     * Where instrument [index] plays: a channel name when it plays every column
     * of the channel, otherwise the channel and column numbers ("Drums col 2").
     */
    fun usedBy(index: Int): List<String> = slots().groupBy { it.channel }.mapNotNull { (_, cols) ->
        val mine = cols.filter { it.instrument == index }
        when {
            mine.isEmpty() -> null
            mine.size == cols.size -> cols.first().channelName
            else -> "${cols.first().channelName} col ${mine.joinToString(", ") { "${it.column + 1}" }}"
        }
    }
}
