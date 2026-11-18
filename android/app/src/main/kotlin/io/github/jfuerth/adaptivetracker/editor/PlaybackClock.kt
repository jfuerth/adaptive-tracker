package io.github.jfuerth.adaptivetracker.editor

import kotlin.math.abs

/**
 * A smooth engine clock for drawing playback. The engine reports where its
 * clock is once per audio callback, and callbacks don't line up with screen
 * frames (they come in bursts, at another rate), so drawing each report as it
 * comes makes scrolling beat. This runs its own clock at the engine's rate,
 * read at each frame's time, and eases it toward the reports: a tenth of the
 * difference each frame, or at once if it is far off.
 */
class PlaybackClock {
    private var valid = false
    private var ticks = 0.0  // engine ticks at [time]
    private var time = 0L    // ns, System.nanoTime's clock

    /** Forget the clock (playback stopped): the next report is taken as it is. */
    fun reset() {
        valid = false
    }

    /**
     * Engine ticks at [frameNanos], given the latest report: the engine was at
     * [reportedTicks] at [stampNanos], running at [ticksPerSecond].
     */
    fun at(frameNanos: Long, reportedTicks: Double, stampNanos: Long, ticksPerSecond: Double): Double {
        val perNano = ticksPerSecond / 1e9
        val reported = reportedTicks + (frameNanos - stampNanos) * perNano
        val predicted = ticks + (frameNanos - time) * perNano
        val error = reported - predicted
        ticks = if (!valid || abs(error) > ticksPerSecond * SNAP_SECONDS) reported else predicted + error * GAIN
        time = frameNanos
        valid = true
        return ticks
    }

    /**
     * Engine ticks at [frameNanos] from a NativeAudio.getPlayheads snapshot,
     * counted from the snapshot's tick ([heads][0]), which its channel values
     * are measured at: add this to them for where they are now.
     */
    fun since(frameNanos: Long, heads: LongArray): Double {
        if (heads.size < CLOCK_VALUES || heads[3] <= 0) return 0.0
        val now = at(frameNanos, heads[0] + heads[1] / 1000.0, heads[2], heads[3] / 1000.0)
        return now - heads[0]
    }

    companion object {
        /** Values before the channels in a getPlayheads snapshot. */
        const val CLOCK_VALUES = 4
        private const val GAIN = 0.1
        private const val SNAP_SECONDS = 0.1
    }
}
