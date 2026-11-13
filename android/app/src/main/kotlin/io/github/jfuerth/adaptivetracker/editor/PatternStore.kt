package io.github.jfuerth.adaptivetracker.editor

import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.snapshots.SnapshotStateList
import io.github.jfuerth.adaptivetracker.NativeAudio

/** Shape of a pattern and the channel it plays on. */
data class PatternInfo(
    val channel: Int,
    val rows: Int,
    /** Speed: effect ticks per row. */
    val ticksPerRow: Int,
    val columns: Int,
    val mode: PitchMode,
    /** Effect ticks per beat (the engine's clock). */
    val ticksPerBeat: Int,
) {
    /** True for a row during which a beat starts (every row when the speed is unknown). */
    fun isBeat(row: Int): Boolean {
        if (ticksPerBeat <= 0 || ticksPerRow <= 0) return true
        val start = row * ticksPerRow
        val nextBeat = (start + ticksPerBeat - 1) / ticksPerBeat * ticksPerBeat
        return nextBeat < start + ticksPerRow
    }

    /** "6 (4 rows/beat)", or just "5" when rows don't divide a beat. */
    val speedLabel: String get() =
        if (ticksPerRow > 0 && ticksPerBeat % ticksPerRow == 0) "$ticksPerRow (${ticksPerBeat / ticksPerRow} rows/beat)"
        else "$ticksPerRow"
}

/**
 * The UI's copy of each pattern. Loaded from the engine the first time a
 * pattern is opened; after that, edits update this copy and are sent to the
 * engine with NativeAudio.setCell.
 */
object PatternStore {
    class Loaded(val info: PatternInfo, val cells: SnapshotStateList<CellData>) {
        fun cell(row: Int, column: Int): CellData = cells[row * info.columns + column]
    }

    private val cache = mutableMapOf<Int, Loaded>()
    private val ticksPerBeat by lazy { NativeAudio.getTicksPerBeat() }

    var names: List<String> = emptyList()
        private set

    /** Forgets everything read from the engine: a different song has loaded. */
    fun reset() {
        cache.clear()
        names = List(NativeAudio.getPatternCount()) { NativeAudio.getPatternName(it) }
    }

    fun get(index: Int): Loaded? = cache[index] ?: load(index)?.also { cache[index] = it }

    /** Updates the UI copy and sends the change to the engine. */
    fun write(index: Int, row: Int, column: Int, cell: CellData) {
        val p = get(index) ?: return
        p.cells[row * p.info.columns + column] = cell
        NativeAudio.setCell(index, row, column, cell.note, cell.accidental, cell.volume, cell.effect, cell.param)
    }

    private fun load(index: Int): Loaded? {
        val raw = NativeAudio.getPatternInfo(index) ?: return null
        val info = PatternInfo(raw[0], raw[1], raw[2], raw[3], PitchMode.entries[raw[4]], ticksPerBeat)
        val bytes = NativeAudio.readPattern(index) ?: return null
        val cells = mutableStateListOf<CellData>()
        for (i in 0 until info.rows * info.columns) cells.add(CellData.fromBytes(bytes, i * 5))
        return Loaded(info, cells)
    }
}
