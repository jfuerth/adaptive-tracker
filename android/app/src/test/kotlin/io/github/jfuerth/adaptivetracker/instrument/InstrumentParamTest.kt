package io.github.jfuerth.adaptivetracker.instrument

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class InstrumentParamTest {
    // Ranges come from the engine at runtime; these stand in for it.
    private val cutoff = ParamSpec(InstrumentParam.CUTOFF, 1, 20f, 20000f)
    private val specs = listOf(
        cutoff,
        ParamSpec(InstrumentParam.SHAPE, 0, 0f, 1f),
        ParamSpec(InstrumentParam.RESONANCE, 2, 0.1f, 20f),
        ParamSpec(InstrumentParam.AMP_ATTACK, 8, 0.0005f, 10f),
        ParamSpec(InstrumentParam.LEVEL, 12, 0f, 1f),
    )

    @Test fun positionsRoundTrip() {
        for (s in specs) {
            for (t in listOf(0f, 0.1f, 0.37f, 0.5f, 0.9f, 1f)) {
                assertEquals("${s.param} at $t", t, s.toPosition(s.fromPosition(t)), 1e-4f)
            }
            assertEquals(s.min, s.fromPosition(0f), s.min * 1e-5f + 1e-6f)
            assertEquals(s.max, s.fromPosition(1f), s.max * 1e-5f)
        }
    }

    @Test fun logCurveHasEqualRatios() {
        assertEquals(632.5f, cutoff.fromPosition(0.5f), 1f)  // geometric mean of 20 Hz and 20 kHz
        assertEquals(0.5f, cutoff.toPosition(632.46f), 1e-3f)
    }

    @Test fun outOfRangeValuesClamp() {
        assertEquals(1f, specs.first { it.param == InstrumentParam.LEVEL }.toPosition(5f))
        assertEquals(0f, specs.first { it.param == InstrumentParam.AMP_ATTACK }.toPosition(0f))
    }

    @Test fun specsMatchTheEngineByKey() {
        val keys = listOf("cutoff", "somethingNew", "shape")
        val ranges = floatArrayOf(20f, 20000f, 0f, 9f, 0f, 1f)
        val found = ParamSpec.fromEngine(keys, ranges)
        assertEquals(ParamSpec(InstrumentParam.CUTOFF, 0, 20f, 20000f), found[InstrumentParam.CUTOFF])
        assertEquals(ParamSpec(InstrumentParam.SHAPE, 2, 0f, 1f), found[InstrumentParam.SHAPE])
        assertNull(found[InstrumentParam.LEVEL])  // the engine didn't list it
        assertEquals(2, found.size)  // the unknown key is ignored
    }

    @Test fun formats() {
        assertEquals("2.2 kHz", InstrumentParam.CUTOFF.format(2200f))
        assertEquals("320 Hz", InstrumentParam.CUTOFF.format(320f))
        assertEquals("250 ms", InstrumentParam.AMP_ATTACK.format(0.25f))
        assertEquals("0.5 ms", InstrumentParam.AMP_ATTACK.format(0.0005f))
        assertEquals("1.50 s", InstrumentParam.AMP_RELEASE.format(1.5f))
        assertEquals("Triangle", InstrumentParam.SHAPE.format(0.5f))
        assertEquals("Saw–tri 0.30", InstrumentParam.SHAPE.format(0.3f))
        assertEquals("High-pass", InstrumentParam.FILTER_MODE.format(1f))
        assertEquals("Off", InstrumentParam.NOISE_MIX.format(0f))
        assertEquals("60% noise", InstrumentParam.NOISE_MIX.format(0.6f))
        assertEquals("Noise only", InstrumentParam.NOISE_MIX.format(1f))
        assertEquals("180 ms", InstrumentParam.NOISE_DECAY.format(0.18f))
        assertEquals("Hold", InstrumentParam.NOISE_DECAY.format(10f))
        assertEquals("+24 st", InstrumentParam.PITCH_ENV.format(24f))
        assertEquals("-7 st", InstrumentParam.PITCH_ENV.format(-7f))
        assertEquals("0 st", InstrumentParam.PITCH_ENV.format(0.2f))
    }

    @Test fun discreteParametersLandOnWholeNumbers() {
        val mode = ParamSpec(InstrumentParam.FILTER_MODE, 17, 0f, 2f)
        assertEquals(1, mode.sliderSteps)  // three positions: two ends and one between
        assertEquals(0f, mode.fromPosition(0.2f))
        assertEquals(1f, mode.fromPosition(0.45f))
        assertEquals(2f, mode.fromPosition(0.8f))
        assertEquals(0.5f, mode.toPosition(1f))
        // The fine-adjust buttons step a whole mode, not a sliver that rounds back.
        assertEquals(1f, mode.fromPosition(mode.toPosition(0f) + mode.nudgeStep(0.005f)))
        assertEquals(0, cutoff.sliderSteps)
        assertEquals(0.005f, cutoff.nudgeStep(0.005f))
    }
}
