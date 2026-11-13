package io.github.jfuerth.adaptivetracker.instrument

import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.exp
import kotlin.math.ln
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.sin
import kotlin.math.sqrt

/** Signal analysis for the live output views: spectrum and oscilloscope trigger. */
object Analysis {
    /**
     * Amplitude spectrum of [samples] (length a power of two) with a Hann
     * window: one value per bin up to Nyquist (n/2 + 1 values), scaled so a
     * full-scale sine centred on a bin reads 1.0.
     */
    fun spectrum(samples: FloatArray): FloatArray {
        val n = samples.size
        require(n >= 2 && n and (n - 1) == 0) { "length must be a power of two" }
        val re = DoubleArray(n) { i -> samples[i] * (0.5 - 0.5 * cos(2 * PI * i / n)) }
        val im = DoubleArray(n)
        fft(re, im)
        // Hann's coherent gain is 0.5, and a real sine splits between two bins.
        val scale = 4.0 / n
        return FloatArray(n / 2 + 1) { k -> (sqrt(re[k] * re[k] + im[k] * im[k]) * scale).toFloat() }
    }

    /** In-place iterative radix-2 FFT. */
    private fun fft(re: DoubleArray, im: DoubleArray) {
        val n = re.size
        var j = 0
        for (i in 1 until n) {
            var bit = n shr 1
            while (j and bit != 0) {
                j = j xor bit
                bit = bit shr 1
            }
            j = j xor bit
            if (i < j) {
                re[i] = re[j].also { re[j] = re[i] }
                im[i] = im[j].also { im[j] = im[i] }
            }
        }
        var len = 2
        while (len <= n) {
            val angle = -2 * PI / len
            val wr = cos(angle)
            val wi = sin(angle)
            for (start in 0 until n step len) {
                var cr = 1.0
                var ci = 0.0
                for (k in 0 until len / 2) {
                    val a = start + k
                    val b = a + len / 2
                    val tr = re[b] * cr - im[b] * ci
                    val ti = re[b] * ci + im[b] * cr
                    re[b] = re[a] - tr
                    im[b] = im[a] - ti
                    re[a] += tr
                    im[a] += ti
                    val nr = cr * wr - ci * wi
                    ci = cr * wi + ci * wr
                    cr = nr
                }
            }
            len = len shl 1
        }
    }

    /**
     * Levels in dB (full-scale sine = 0 dB) for [bands] log-spaced bands from
     * [minHz] to [maxHz], lowest first. Each band takes its loudest bin; bands
     * narrower than a bin interpolate between bins.
     */
    fun logBands(spectrum: FloatArray, sampleRate: Float, bands: Int, minHz: Float, maxHz: Float): FloatArray {
        val binHz = sampleRate / ((spectrum.size - 1) * 2)
        val ratio = ln(maxHz / minHz)
        fun edge(b: Int) = minHz * exp(ratio * b / bands)
        return FloatArray(bands) { b ->
            val lo = edge(b) / binHz
            val hi = edge(b + 1) / binHz
            var peak = 0f
            var k = kotlin.math.ceil(lo).toInt()
            while (k <= hi && k < spectrum.size) {
                peak = max(peak, spectrum[k])
                k++
            }
            if (peak == 0f) {  // no bin inside: interpolate at the band's centre
                val c = (lo + hi) / 2
                val i = c.toInt().coerceIn(0, spectrum.size - 2)
                val t = (c - i).coerceIn(0f, 1f)
                peak = spectrum[i] * (1 - t) + spectrum[i + 1] * t
            }
            if (peak <= 0f) Float.NEGATIVE_INFINITY else 20f * log10(peak)
        }
    }

    /**
     * Where an oscilloscope window of [window] samples should start so the
     * waveform holds still: just after the latest rising zero crossing that
     * leaves a full window, searching back up to [searchSpan] samples from the
     * newest possible start. Falls back to the newest window.
     */
    fun triggerStart(samples: FloatArray, window: Int, searchSpan: Int): Int {
        val newest = samples.size - window
        if (newest <= 0) return 0
        val oldest = (newest - searchSpan).coerceAtLeast(1)
        for (i in newest downTo oldest) {
            if (samples[i - 1] < 0f && samples[i] >= 0f) return i
        }
        return newest
    }

    /**
     * Analyser ballistics: each band jumps up to its new level at once but
     * falls by at most [maxFallDb] since the last frame. Levels stay at or
     * above [floorDb] (silence, -inf, reads as the floor).
     */
    fun fall(previous: FloatArray, current: FloatArray, maxFallDb: Float, floorDb: Float): FloatArray =
        FloatArray(current.size) { i ->
            val now = current[i].coerceAtLeast(floorDb)
            val before = previous.getOrElse(i) { floorDb }
            if (now >= before) now else maxOf(now, before - maxFallDb)
        }

    /** Normalised correlation of two equal-length signals: 1 for the same shape at any level. */
    fun similarity(a: FloatArray, b: FloatArray): Float {
        var dot = 0f
        for (i in a.indices) dot += a[i] * b[i]
        val norm = kotlin.math.sqrt(energy(a) * energy(b))
        return if (norm > 0f) dot / norm else 0f
    }

    fun energy(x: FloatArray): Float {
        var e = 0f
        for (v in x) e += v * v
        return e
    }

    /**
     * Correlation trigger: where in [samples] a window the size of [reference]
     * best matches it, searching back [searchSpan] samples from the newest
     * window. Unlike a zero-crossing trigger this holds still for any wave
     * shape, e.g. a resonant saw that crosses zero several times per cycle.
     * Searches every 4th sample of every 4th offset, then refines.
     */
    fun alignTo(reference: FloatArray, samples: FloatArray, searchSpan: Int): Int {
        val window = reference.size
        val newest = samples.size - window
        if (newest <= 0) return 0
        val oldest = (newest - searchSpan).coerceAtLeast(0)
        fun score(start: Int, step: Int): Float {
            var sum = 0f
            var k = 0
            while (k < window) {
                sum += reference[k] * samples[start + k]
                k += step
            }
            return sum
        }
        var best = newest
        var bestScore = Float.NEGATIVE_INFINITY
        var s = newest
        while (s >= oldest) {
            val sc = score(s, 4)
            if (sc > bestScore) {
                bestScore = sc
                best = s
            }
            s -= 4
        }
        val coarse = best
        bestScore = Float.NEGATIVE_INFINITY
        for (c in (coarse - 4).coerceAtLeast(oldest)..(coarse + 4).coerceAtMost(newest)) {
            val sc = score(c, 1)
            if (sc > bestScore) {
                bestScore = sc
                best = c
            }
        }
        return best
    }
}
