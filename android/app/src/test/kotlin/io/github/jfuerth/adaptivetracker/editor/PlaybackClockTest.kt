package io.github.jfuerth.adaptivetracker.editor

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class PlaybackClockTest {
    private val rate = 192.0  // engine ticks a second at 120 BPM
    private val frame = 16_666_667L

    @Test fun takesTheFirstReportAsItIs() {
        val clock = PlaybackClock()
        // 10 ticks at t=0: half a second later it's 96 ticks on.
        assertEquals(106.0, clock.at(500_000_000L, 10.0, 0L, rate), 1e-6)
    }

    @Test fun movesEvenlyThroughJitteryReports() {
        val clock = PlaybackClock()
        // Reports every 10 ms, each stamped up to 3 ms late (callbacks in bursts).
        var last = Double.NaN
        val steps = mutableListOf<Double>()
        for (f in 0 until 120) {
            val t = f * frame
            val reportTime = (t / 10_000_000L) * 10_000_000L
            val jitter = ((f * 7919) % 7 - 3) * 1_000_000L
            val now = clock.at(t, reportTime * rate / 1e9, reportTime + jitter, rate)
            if (!last.isNaN() && f > 30) steps += now - last
            last = now
        }
        val ideal = rate * frame / 1e9
        val worst = steps.maxOf { kotlin.math.abs(it - ideal) }
        assertTrue("steps vary by $worst ticks (ideal $ideal)", worst < ideal * 0.1)
    }

    @Test fun jumpsWhenFarOff() {
        val clock = PlaybackClock()
        clock.at(0L, 0.0, 0L, rate)
        // A report half a second away from the prediction is taken at once.
        assertEquals(96.0 + rate * frame / 1e9, clock.at(frame, 96.0, 0L, rate), 1e-6)
    }

    @Test fun countsFromTheSnapshotTick() {
        val clock = PlaybackClock()
        // Tick 1000, 0.5 into the next, stamped at t=0, 192 ticks/s; read 1/192 s later.
        val heads = longArrayOf(1000, 500, 0, 192_000)
        assertEquals(1.5, clock.since(1_000_000_000L / 192, heads), 1e-6)
    }
}
