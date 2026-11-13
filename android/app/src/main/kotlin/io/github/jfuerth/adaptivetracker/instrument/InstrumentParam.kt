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

private const val TIME_MIN = 0.0005f
private const val TIME_MAX = 10f
private const val Q_MIN = 0.1f
private const val Q_MAX = 20f
private const val RATE_MIN = 0.1f
private const val RATE_MAX = 20f

/**
 * An instrument parameter. The order and ranges match
 * `adaptive::Instrument::Param` in core/engine/include/adaptive/instrument.hpp.
 */
enum class InstrumentParam(val label: String, val min: Float, val max: Float, val curve: Curve) {
    SHAPE("Shape", 0f, 1f, Curve.LINEAR),
    CUTOFF("Cutoff", 20f, 20000f, Curve.LOG),
    RESONANCE("Resonance", Q_MIN, Q_MAX, Curve.SQUARE),
    FILTER_ENV_AMOUNT("Amount", 0f, 1f, Curve.LINEAR),
    FILTER_ATTACK("Attack", TIME_MIN, TIME_MAX, Curve.LOG),
    FILTER_DECAY("Decay", TIME_MIN, TIME_MAX, Curve.LOG),
    FILTER_SUSTAIN("Sustain", 0f, 1f, Curve.LINEAR),
    FILTER_RELEASE("Release", TIME_MIN, TIME_MAX, Curve.LOG),
    AMP_ATTACK("Attack", TIME_MIN, TIME_MAX, Curve.LOG),
    AMP_DECAY("Decay", TIME_MIN, TIME_MAX, Curve.LOG),
    AMP_SUSTAIN("Sustain", 0f, 1f, Curve.LINEAR),
    AMP_RELEASE("Release", TIME_MIN, TIME_MAX, Curve.LOG),
    LEVEL("Level", 0f, 1f, Curve.LINEAR),
    VIBRATO_RATE("Rate", RATE_MIN, RATE_MAX, Curve.LOG),
    VIBRATO_DEPTH("Depth", 0f, 2f, Curve.SQUARE),
    TREMOLO_RATE("Rate", RATE_MIN, RATE_MAX, Curve.LOG),
    TREMOLO_DEPTH("Depth", 0f, 1f, Curve.LINEAR);

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

    fun clamp(value: Float) = value.coerceIn(min, max)

    /** Slider position 0..1 for [value]. */
    fun toPosition(value: Float): Float {
        val v = clamp(value)
        return when (curve) {
            Curve.LINEAR -> (v - min) / (max - min)
            Curve.LOG -> (ln(v / min) / ln(max / min))
            Curve.SQUARE -> sqrt((v - min) / (max - min))
        }.coerceIn(0f, 1f)
    }

    /** Value for slider position [t] (0..1). */
    fun fromPosition(t: Float): Float {
        val p = t.coerceIn(0f, 1f)
        return clamp(
            when (curve) {
                Curve.LINEAR -> min + (max - min) * p
                Curve.LOG -> min * exp(ln(max / min) * p)
                Curve.SQUARE -> min + (max - min) * p * p
            },
        )
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
