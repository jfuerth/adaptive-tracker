package io.github.jfuerth.adaptivetracker.instrument

import java.util.Locale
import kotlin.math.abs
import kotlin.math.ln
import kotlin.math.exp
import kotlin.math.sqrt

/** How a slider's position (0..1) maps to a parameter's value. */
enum class Curve {
    LINEAR,
    /** Equal ratios per step: for Hz and times. */
    LOG,
    /** Finer steps near the minimum (same as the 5xx resonance effect). */
    SQUARE,
}

/**
 * An instrument parameter as the editor presents it: label, slider curve and
 * formatting. Its range, default and position in the engine come from the
 * engine at runtime (see [ParamSpec]), matched by [key].
 */
enum class InstrumentParam(val key: String, val label: String, val curve: Curve) {
    SHAPE("shape", "Shape", Curve.LINEAR),
    CUTOFF("cutoff", "Cutoff", Curve.LOG),
    RESONANCE("resonance", "Resonance", Curve.SQUARE),
    FILTER_ENV_AMOUNT("filterEnvAmount", "Amount", Curve.LINEAR),
    FILTER_ATTACK("filterAttack", "Attack", Curve.LOG),
    FILTER_DECAY("filterDecay", "Decay", Curve.LOG),
    FILTER_SUSTAIN("filterSustain", "Sustain", Curve.LINEAR),
    FILTER_RELEASE("filterRelease", "Release", Curve.LOG),
    AMP_ATTACK("ampAttack", "Attack", Curve.LOG),
    AMP_DECAY("ampDecay", "Decay", Curve.LOG),
    AMP_SUSTAIN("ampSustain", "Sustain", Curve.LINEAR),
    AMP_RELEASE("ampRelease", "Release", Curve.LOG),
    LEVEL("level", "Level", Curve.LINEAR),
    VIBRATO_RATE("vibratoRate", "Rate", Curve.LOG),
    VIBRATO_DEPTH("vibratoDepth", "Depth", Curve.SQUARE),
    TREMOLO_RATE("tremoloRate", "Rate", Curve.LOG),
    TREMOLO_DEPTH("tremoloDepth", "Depth", Curve.LINEAR);

    /** Where in the signal flow it lives, for the slider's title. */
    val group: String
        get() = when (this) {
            SHAPE -> "Oscillator"
            CUTOFF, RESONANCE -> "Filter"
            FILTER_ENV_AMOUNT, FILTER_ATTACK, FILTER_DECAY, FILTER_SUSTAIN, FILTER_RELEASE -> "Filter envelope"
            AMP_ATTACK, AMP_DECAY, AMP_SUSTAIN, AMP_RELEASE -> "Amp envelope"
            LEVEL -> "Amp"
            VIBRATO_RATE, VIBRATO_DEPTH -> "Vibrato"
            TREMOLO_RATE, TREMOLO_DEPTH -> "Tremolo"
        }

    /** The value as shown on the chip and slider. */
    fun format(value: Float): String = when (this) {
        SHAPE -> when {
            value < 0.02f -> "Saw"
            abs(value - 0.5f) < 0.02f -> "Triangle"
            value > 0.98f -> "Square"
            value < 0.5f -> "Saw–tri %.2f".format(Locale.US, value)
            else -> "Tri–sq %.2f".format(Locale.US, value)
        }
        CUTOFF -> hz(value)
        RESONANCE -> "Q %.2f".format(Locale.US, value)
        FILTER_ATTACK, FILTER_DECAY, FILTER_RELEASE, AMP_ATTACK, AMP_DECAY, AMP_RELEASE -> seconds(value)
        VIBRATO_RATE, TREMOLO_RATE -> hz(value)
        VIBRATO_DEPTH -> "%.2f st".format(Locale.US, value)
        else -> "%.2f".format(Locale.US, value)
    }

    companion object {
        fun hz(v: Float): String = when {
            v >= 1000f -> "%.1f kHz".format(Locale.US, v / 1000f)
            v >= 10f -> "${v.toInt()} Hz"
            else -> "%.1f Hz".format(Locale.US, v)
        }

        fun seconds(v: Float): String = when {
            v < 0.01f -> "%.1f ms".format(Locale.US, v * 1000f)
            v < 1f -> "${(v * 1000f).toInt()} ms"
            else -> "%.2f s".format(Locale.US, v)
        }
    }
}

/**
 * A parameter as the engine defines it: its index in the engine's parameter
 * list and its range. Maps slider positions to values with the parameter's curve.
 */
data class ParamSpec(val param: InstrumentParam, val index: Int, val min: Float, val max: Float) {
    fun clamp(value: Float) = value.coerceIn(min, max)

    /** Slider position 0..1 for [value]. */
    fun toPosition(value: Float): Float {
        val v = clamp(value)
        if (max <= min) return 0f
        return when (param.curve) {
            Curve.LINEAR -> (v - min) / (max - min)
            Curve.LOG -> ln(v / min) / ln(max / min)
            Curve.SQUARE -> sqrt((v - min) / (max - min))
        }.coerceIn(0f, 1f)
    }

    /** Value for slider position [t] (0..1). */
    fun fromPosition(t: Float): Float {
        val p = t.coerceIn(0f, 1f)
        return clamp(
            when (param.curve) {
                Curve.LINEAR -> min + (max - min) * p
                Curve.LOG -> min * exp(ln(max / min) * p)
                Curve.SQUARE -> min + (max - min) * p * p
            },
        )
    }

    companion object {
        /**
         * Specs for the parameters the editor knows, from the engine's [keys]
         * and [ranges] ([min, max] per key). Engine parameters the editor
         * doesn't know are left out; editor parameters the engine lacks are absent.
         */
        fun fromEngine(keys: List<String>, ranges: FloatArray): Map<InstrumentParam, ParamSpec> =
            keys.mapIndexedNotNull { i, key ->
                val p = InstrumentParam.entries.firstOrNull { it.key == key } ?: return@mapIndexedNotNull null
                p to ParamSpec(p, i, ranges[i * 2], ranges[i * 2 + 1])
            }.toMap()
    }
}
