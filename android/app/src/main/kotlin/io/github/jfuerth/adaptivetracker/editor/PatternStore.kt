package io.github.jfuerth.adaptivetracker.editor

import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.snapshots.SnapshotStateList
import io.github.jfuerth.adaptivetracker.NativeAudio

/** Shape of a pattern and the channel it plays on. */
data class PatternInfo(
    val channel: Int,
    val rows: Int,
    val rowsPerBeat: Int,
    val columns: Int,
    val mode: PitchMode,
)

/**
 * The UI's copy of each pattern. Loaded from the engine the first time a
 * pattern is opened; after that, edits update this copy and are sent to the
 * engine with NativeAudio.setCell, so the UI never reads memory the audio
 * thread writes.
 */
object PatternStore {
    class Loaded(val info: PatternInfo, val cells: SnapshotStateList<CellData>) {
        fun cell(row: Int, column: Int): CellData = cells[row * info.columns + column]
    }

    private val cache = mutableMapOf<Int, Loaded>()

    val names: List<String> by lazy { List(NativeAudio.getPatternCount()) { NativeAudio.getPatternName(it) } }

    fun get(index: Int): Loaded? = cache[index] ?: load(index)?.also { cache[index] = it }

    /** Updates the UI copy and sends the change to the engine. */
    fun write(index: Int, row: Int, column: Int, cell: CellData) {
        val p = get(index) ?: return
        p.cells[row * p.info.columns + column] = cell
        NativeAudio.setCell(index, row, column, cell.note, cell.accidental, cell.volume, cell.effect, cell.param)
    }

    private fun load(index: Int): Loaded? {
        val raw = NativeAudio.getPatternInfo(index) ?: return null
        val info = PatternInfo(raw[0], raw[1], raw[2], raw[3], PitchMode.entries[raw[4]])
        val bytes = NativeAudio.readPattern(index) ?: return null
        val cells = mutableStateListOf<CellData>()
        for (i in 0 until info.rows * info.columns) cells.add(CellData.fromBytes(bytes, i * 5))
        return Loaded(info, cells)
    }
}
