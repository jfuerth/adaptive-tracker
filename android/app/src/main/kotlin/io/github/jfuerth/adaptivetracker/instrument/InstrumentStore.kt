package io.github.jfuerth.adaptivetracker.instrument

import androidx.compose.runtime.snapshots.SnapshotStateList
import androidx.compose.runtime.toMutableStateList
import io.github.jfuerth.adaptivetracker.NativeAudio

/**
 * The UI's copy of each instrument. Each one is read from the engine once,
 * before it is first edited; after that, edits update this copy and are sent
 * to the engine, which applies them on the audio thread.
 */
object InstrumentStore {
    val names: List<String> by lazy { List(NativeAudio.getInstrumentCount()) { NativeAudio.getInstrumentName(it) } }

    private val loaded = HashMap<Int, SnapshotStateList<Float>>()

    /** Instrument [index]'s values in InstrumentParam order, or null if it can't be read. */
    fun values(index: Int): SnapshotStateList<Float>? = loaded[index] ?: NativeAudio.readInstrument(index)
        ?.takeIf { it.size == InstrumentParam.entries.size }
        ?.toList()?.toMutableStateList()
        ?.also { loaded[index] = it }

    fun set(index: Int, param: InstrumentParam, value: Float) {
        val v = param.clamp(value)
        val values = values(index) ?: return
        if (values[param.ordinal] == v) return
        values[param.ordinal] = v
        NativeAudio.setInstrumentParam(index, param.ordinal, v)
    }

    /**
     * Where instrument [index] plays: a channel name when it plays every column
     * of the channel, otherwise the channel and column numbers ("Drums col 2").
     */
    fun usedBy(index: Int): List<String> {
        val uses = mutableListOf<String>()
        for (ch in 0 until NativeAudio.getChannelCount()) {
            val columns = generateSequence(0) { it + 1 }
                .map { NativeAudio.getColumnInstrument(ch, it) }
                .takeWhile { it >= 0 }
                .toList()
            val mine = columns.indices.filter { columns[it] == index }
            if (mine.isEmpty()) continue
            val name = NativeAudio.getChannelName(ch)
            uses += if (mine.size == columns.size) name else "$name col ${mine.joinToString(", ") { "${it + 1}" }}"
        }
        return uses
    }
}
