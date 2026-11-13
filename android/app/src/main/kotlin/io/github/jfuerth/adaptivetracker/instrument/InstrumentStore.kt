package io.github.jfuerth.adaptivetracker.instrument

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshots.SnapshotStateList
import androidx.compose.runtime.toMutableStateList
import io.github.jfuerth.adaptivetracker.NativeAudio

/**
 * The UI's copy of each instrument. Each one is read from the engine once,
 * before it is first edited; after that, edits update this copy and are sent
 * to the engine, which applies them on the audio thread.
 *
 * Names live only here: the engine refers to instruments by index.
 */
object InstrumentStore {
    /** Each instrument's name, by index. Starts with the song's own names. */
    val names: SnapshotStateList<String> by lazy {
        List(NativeAudio.getInstrumentCount()) { i ->
            NativeAudio.getInstrumentName(i).ifEmpty { "Instrument ${i + 1}" }
        }.toMutableStateList()
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
        val index = NativeAudio.addInstrument(source)
        if (index < 0) return null
        names += if (copyOf != null) uniqueName("${names[copyOf]} copy") else uniqueName("Instrument ${index + 1}")
        return index
    }

    fun rename(index: Int, name: String) {
        val trimmed = name.trim()
        if (index in names.indices && trimmed.isNotEmpty()) names[index] = trimmed
    }

    private fun uniqueName(base: String): String {
        if (base !in names) return base
        return generateSequence(2) { it + 1 }.map { "$base $it" }.first { it !in names }
    }

    /** [channel]'s [column] plays instrument [index] from now on. */
    fun assign(channel: Int, column: Int, index: Int) {
        if (NativeAudio.assignInstrument(channel, column, index)) assignments++
    }

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
