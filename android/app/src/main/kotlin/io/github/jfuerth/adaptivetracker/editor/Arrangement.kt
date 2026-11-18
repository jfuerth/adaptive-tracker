package io.github.jfuerth.adaptivetracker.editor

import kotlin.math.roundToLong

/**
 * A section laid out in time: one lane per channel, each playing its
 * sequence of patterns over and over. Times are in effect ticks from the
 * section's start (when every channel starts its sequence together).
 */
class Arrangement(val lanes: List<Lane>, val ticksPerBeat: Int, val beatsPerBar: Int) {
    /** One pattern in a channel's sequence. [start] is in ticks from the sequence's start. */
    data class Entry(val pattern: Int, val start: Int, val rows: Int, val ticksPerRow: Int) {
        val length: Int get() = rows * ticksPerRow
        val end: Int get() = start + length
    }

    /** One time round an entry: it starts at [start] ticks into the section. */
    data class Placed(val entry: Entry, val index: Int, val start: Long) {
        val end: Long get() = start + entry.length
        /** The row playing [tick] ticks into the section, or -1 outside this instance. */
        fun rowAt(tick: Long): Int =
            if (tick < start || tick >= end) -1 else ((tick - start) / entry.ticksPerRow).toInt()
        /** When row [row] of this instance starts. */
        fun rowStart(row: Int): Long = start + row.toLong() * entry.ticksPerRow
    }

    /** Where a channel is playing: [row] of [placed], the line at [tick] ticks into the section. */
    data class Playhead(val placed: Placed, val row: Int, val tick: Float)

    /** A channel's lane: its sequence and how many columns its patterns have. */
    class Lane(val channel: Int, val columns: Int, val entries: List<Entry>) {
        /** Ticks the sequence takes to come round again (0: nothing to play). */
        val length: Int = entries.lastOrNull()?.end ?: 0

        /** The instance playing at [tick] (ticks into the section), or null. */
        fun at(tick: Long): Placed? {
            if (length <= 0 || tick < 0) return null
            val loop = tick / length
            val inLoop = (tick % length).toInt()
            val i = entries.indexOfLast { it.start <= inLoop }
            return if (i < 0) null else Placed(entries[i], i, loop * length + entries[i].start)
        }

        /** Instances overlapping ticks [from] to [to], in time order. */
        fun between(from: Long, to: Long): Sequence<Placed> = sequence {
            if (length <= 0 || to < 0) return@sequence
            var loop = maxOf(0L, from) / length
            while (loop * length <= to) {
                for ((i, e) in entries.withIndex()) {
                    val start = loop * length + e.start
                    if (start > to) return@sequence
                    if (start + e.length > from) yield(Placed(e, i, start))
                }
                loop++
            }
        }

        /**
         * Places what the engine says the channel is playing: [pattern] at
         * sequence [entry], [row], [intoRow] ticks into it. [sinceStart] (ticks
         * since the sequence started) only picks the time round, so if the
         * channel has drifted from this layout (a pattern's length changed
         * while it played, a bump back), the playhead is still drawn in the
         * row that is playing. Null if the lane doesn't have that entry.
         */
        fun playhead(pattern: Int, entry: Int, row: Int, intoRow: Float, sinceStart: Float): Playhead? {
            val e = entries.getOrNull(entry) ?: return null
            if (e.pattern != pattern || row !in 0 until e.rows) return null
            val inLoop = e.start + row * e.ticksPerRow + intoRow.coerceIn(0f, e.ticksPerRow.toFloat())
            val loop = ((sinceStart - inLoop) / length).roundToLong().coerceAtLeast(0L)
            val placed = Placed(e, entry, loop * length + e.start)
            return Playhead(placed, row, placed.rowStart(row) + intoRow.coerceIn(0f, e.ticksPerRow.toFloat()))
        }

        /** The first instance of [pattern] at or after [tick], or null if the lane doesn't play it. */
        fun firstOf(pattern: Int, tick: Long = 0): Placed? {
            if (length <= 0) return null
            val loop = maxOf(0L, tick) / length
            for (l in loop..loop + 1) {
                for ((i, e) in entries.withIndex()) {
                    val start = l * length + e.start
                    if (e.pattern == pattern && start + e.length > tick) return Placed(e, i, start)
                }
            }
            return null
        }
    }

    val barTicks: Int get() = ticksPerBeat * beatsPerBar

    companion object {
        /**
         * Builds the arrangement from each channel's sequence ([sequences],
         * pattern indexes in order) and the patterns' shapes ([info]).
         * Patterns that can't be read are left out.
         */
        fun build(
            sequences: List<List<Int>>,
            columns: List<Int>,
            info: (Int) -> PatternInfo?,
            ticksPerBeat: Int,
            beatsPerBar: Int,
        ): Arrangement {
            val lanes = sequences.mapIndexed { channel, seq ->
                var t = 0
                val entries = seq.mapNotNull { p ->
                    info(p)?.let { i -> Entry(p, t, i.rows, i.ticksPerRow).also { t += it.length } }
                }
                Lane(channel, columns.getOrElse(channel) { 1 }, entries)
            }
            return Arrangement(lanes, ticksPerBeat, beatsPerBar.coerceAtLeast(1))
        }
    }
}
