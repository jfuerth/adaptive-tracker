package io.github.jfuerth.adaptivetracker.instrument

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.PI
import kotlin.math.sin

class AnalysisTest {
    private fun sine(n: Int, hz: Double, amp: Float = 1f, rate: Double = 48000.0) =
        FloatArray(n) { (amp * sin(2 * PI * hz * it / rate)).toFloat() }

    @Test
    fun fullScaleSineOnABinReadsOne() {
        val n = 2048
        val bin = 40
        val spectrum = Analysis.spectrum(sine(n, bin * 48000.0 / n))
        assertEquals(n / 2 + 1, spectrum.size)
        assertEquals(1f, spectrum[bin], 0.01f)
        assertEquals(bin, spectrum.indices.maxBy { spectrum[it] })
    }

    @Test
    fun aSineLandsInItsBandAtAboutZeroDb() {
        val bands = Analysis.logBands(Analysis.spectrum(sine(2048, 1000.0, 0.5f)), 48000f, 96, 30f, 16000f)
        val loudest = bands.indices.maxBy { bands[it] }
        // Band edges: 30 Hz * (16000/30)^(b/96); 1 kHz falls in band 53.
        assertEquals(53, loudest)
        assertEquals(-6f, bands[loudest], 1.5f)  // half amplitude; a bin edge can lose up to 1.4 dB
        assertTrue(bands[10] < -60f)
    }

    @Test
    fun silenceIsMinusInfinity() {
        val bands = Analysis.logBands(Analysis.spectrum(FloatArray(512)), 48000f, 16, 30f, 16000f)
        assertTrue(bands.all { it == Float.NEGATIVE_INFINITY })
    }

    @Test
    fun scopeTriggersOnTheLatestRisingZeroCrossing() {
        val s = sine(3000, 100.0)  // period 480 samples; rising crossings at multiples of 480
        val start = Analysis.triggerStart(s, 1536, 1200)
        assertTrue(start % 480 <= 1)  // float rounding can put the crossing one sample late
        assertTrue(start <= 3000 - 1536 && start > 3000 - 1536 - 480)
    }
}

class FallTest {
    @Test
    fun levelsRiseAtOnceAndFallGently() {
        val prev = floatArrayOf(-10f, -50f, -80f)
        val now = floatArrayOf(-40f, -20f, Float.NEGATIVE_INFINITY)
        val out = Analysis.fall(prev, now, maxFallDb = 5f, floorDb = -90f)
        assertEquals(-15f, out[0], 0f)   // falls by at most 5 dB
        assertEquals(-20f, out[1], 0f)   // rises straight away
        assertEquals(-85f, out[2], 0f)   // silence falls towards the floor
    }
}
