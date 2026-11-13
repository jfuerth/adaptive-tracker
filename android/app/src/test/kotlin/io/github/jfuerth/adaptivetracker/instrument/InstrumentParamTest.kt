package io.github.jfuerth.adaptivetracker.instrument

import org.junit.Assert.assertEquals
import org.junit.Test

class InstrumentParamTest {
    @Test fun positionsRoundTrip() {
        for (p in InstrumentParam.entries) {
            for (t in listOf(0f, 0.1f, 0.37f, 0.5f, 0.9f, 1f)) {
                assertEquals("$p at $t", t, p.toPosition(p.fromPosition(t)), 1e-4f)
            }
            assertEquals(p.min, p.fromPosition(0f), p.min * 1e-5f + 1e-6f)
            assertEquals(p.max, p.fromPosition(1f), p.max * 1e-5f)
        }
    }

    @Test fun logCurveHasEqualRatios() {
        val c = InstrumentParam.CUTOFF
        assertEquals(632.5f, c.fromPosition(0.5f), 1f)  // geometric mean of 20 Hz and 20 kHz
        assertEquals(0.5f, c.toPosition(632.46f), 1e-3f)
    }

    @Test fun outOfRangeValuesClamp() {
        assertEquals(1f, InstrumentParam.LEVEL.toPosition(5f))
        assertEquals(0f, InstrumentParam.AMP_ATTACK.toPosition(0f))
    }

    // Order must match adaptive::Instrument::Param.
    @Test fun orderMatchesTheEngine() {
        assertEquals(17, InstrumentParam.entries.size)
        assertEquals(1, InstrumentParam.CUTOFF.ordinal)
        assertEquals(12, InstrumentParam.LEVEL.ordinal)
        assertEquals(16, InstrumentParam.TREMOLO_DEPTH.ordinal)
    }

    @Test fun formats() {
        assertEquals("2.2 kHz", InstrumentParam.CUTOFF.format(2200f))
        assertEquals("320 Hz", InstrumentParam.CUTOFF.format(320f))
        assertEquals("250 ms", InstrumentParam.AMP_ATTACK.format(0.25f))
        assertEquals("0.5 ms", InstrumentParam.AMP_ATTACK.format(0.0005f))
        assertEquals("1.50 s", InstrumentParam.AMP_RELEASE.format(1.5f))
        assertEquals("Triangle", InstrumentParam.SHAPE.format(0.5f))
        assertEquals("Saw–tri 0.30", InstrumentParam.SHAPE.format(0.3f))
    }

    @Test fun waveformMorph() {
        assertEquals(-0.5f, Waveform.sample(0f, 0.25f), 1e-6f)  // saw
        assertEquals(0f, Waveform.sample(0.5f, 0.25f), 1e-6f)   // triangle
        assertEquals(1f, Waveform.sample(1f, 0.25f), 1e-6f)     // square
    }
}
