package io.github.jfuerth.adaptivetracker.instrument

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import io.github.jfuerth.adaptivetracker.NativeAudio
import kotlin.math.ln

private const val SAMPLE_RATE = 48000f
/** 4096 points: 11.7 Hz bins, so the low octaves still resolve. */
private const val FFT_SIZE = 4096
/** The oscilloscope shows this many samples (32 ms). */
private const val SCOPE_WINDOW = 1536
/**
 * How far back the scope searches for the best match with the last frame (or
 * for a zero crossing): one full period of notes down to ~40 Hz.
 */
private const val SCOPE_SEARCH = 1200
private const val READ_SIZE = FFT_SIZE
/** The scope keeps its reference while frames match it at least this well (1 = identical shape). */
private const val KEEP_REFERENCE_SIMILARITY = 0.9f
/** Below this (sum of squares), the scope's reference counts as silence. */
private const val MIN_REFERENCE_ENERGY = 1e-4f

private const val BANDS = 160
private const val MIN_HZ = 30f
private const val MAX_HZ = 16000f
private const val FLOOR_DB = -90f
/** How fast a band falls when its level drops, like an analyser's release. */
private const val FALL_DB_PER_SECOND = 60f

/**
 * Live view of one instrument's output: an oscilloscope over a spectrum
 * (level against frequency, both on log scales, falling back gently). Everything the instrument plays is shown (keyboard previews
 * and any columns playing it), before channel effects and the master stage.
 */
@Composable
fun OutputMonitor(instrument: Int, modifier: Modifier = Modifier) {
    DisposableEffect(instrument) {
        NativeAudio.setMonitor(instrument)
        onDispose { NativeAudio.setMonitor(-1) }
    }

    val colors = MaterialTheme.colorScheme
    val background = colors.surfaceContainerLowest
    var scope by remember { mutableStateOf(FloatArray(0)) }
    var levels by remember { mutableStateOf(FloatArray(BANDS) { FLOOR_DB }) }

    LaunchedEffect(Unit) {
        var last = 0L
        var reference: FloatArray? = null
        while (true) {
            val now = withFrameNanos { it }
            val seconds = if (last == 0L) 0f else (now - last) / 1e9f
            last = now
            val samples = NativeAudio.readMonitor(READ_SIZE) ?: continue
            // Line each frame up with what was shown before; with nothing to
            // match (start, or after silence), use a zero crossing.
            val ref = reference
            val start = if (ref != null && Analysis.energy(ref) > MIN_REFERENCE_ENERGY) {
                Analysis.alignTo(ref, samples, SCOPE_SEARCH)
            } else {
                Analysis.triggerStart(samples, SCOPE_WINDOW, SCOPE_SEARCH)
            }
            val shown = samples.copyOfRange(start, start + SCOPE_WINDOW)
            scope = shown
            // Keep matching the same reference while the sound keeps its shape,
            // so rounding can't make the picture creep; take a new one when
            // the shape changes (a new note, a filter sweep) or after silence.
            if (ref == null || Analysis.energy(ref) <= MIN_REFERENCE_ENERGY ||
                Analysis.similarity(ref, shown) < KEEP_REFERENCE_SIMILARITY
            ) {
                reference = shown
            }

            val bands = Analysis.logBands(Analysis.spectrum(samples), SAMPLE_RATE, BANDS, MIN_HZ, MAX_HZ)
            levels = Analysis.fall(levels, bands, FALL_DB_PER_SECOND * seconds, FLOOR_DB)
        }
    }

    val label = MaterialTheme.typography.labelSmall
    Column(modifier) {
        Box {
            val trace = colors.tertiary
            val grid = colors.outlineVariant
            Canvas(Modifier.fillMaxWidth().height(64.dp)) {
                drawRect(background)
                drawLine(grid, Offset(0f, size.height / 2), Offset(size.width, size.height / 2))
                val s = scope
                if (s.isEmpty()) return@Canvas
                val path = Path()
                // One point per pixel column: the sample nearest it.
                val points = size.width.toInt().coerceAtLeast(2)
                for (x in 0 until points) {
                    val v = s[(x.toLong() * (s.size - 1) / (points - 1)).toInt()].coerceIn(-1f, 1f)
                    val y = size.height * (0.5f - 0.48f * v)
                    if (x == 0) path.moveTo(0f, y) else path.lineTo(x.toFloat(), y)
                }
                drawPath(path, trace, style = Stroke(width = 1.5.dp.toPx()))
            }
            Text("32 ms", style = label, color = colors.onSurfaceVariant,
                modifier = Modifier.align(Alignment.TopEnd).padding(4.dp))
        }
        Box(Modifier.padding(top = 2.dp)) {
            val fill = colors.tertiary.copy(alpha = 0.35f)
            val line = colors.tertiary
            val grid = colors.outlineVariant
            val measurer = rememberTextMeasurer()
            val gridLabel = label.copy(color = colors.onSurfaceVariant)
            Canvas(Modifier.fillMaxWidth().height(96.dp)) {
                drawRect(background)
                fun x(hz: Float) = size.width * ln(hz / MIN_HZ) / ln(MAX_HZ / MIN_HZ)
                fun y(db: Float) = size.height * (db / FLOOR_DB).coerceIn(0f, 1f)
                for ((hz, name) in GRID_HZ) {
                    drawLine(grid, Offset(x(hz), 0f), Offset(x(hz), size.height))
                    val text = measurer.measure(name, gridLabel)
                    drawText(text, topLeft = Offset(x(hz) + 3.dp.toPx(), size.height - text.size.height - 2.dp.toPx()))
                }
                for (db in GRID_DB) drawLine(grid, Offset(0f, y(db)), Offset(size.width, y(db)))
                val l = levels
                val outline = Path()
                for (b in l.indices) {
                    val px = size.width * (b + 0.5f) / l.size
                    if (b == 0) outline.moveTo(px, y(l[b])) else outline.lineTo(px, y(l[b]))
                }
                val area = Path().apply {
                    addPath(outline)
                    lineTo(size.width * (l.size - 0.5f) / l.size, size.height)
                    lineTo(size.width * 0.5f / l.size, size.height)
                    close()
                }
                drawPath(area, fill)
                drawPath(outline, line, style = Stroke(width = 1.5.dp.toPx()))
            }
            Text("0 dB", style = label, color = colors.onSurfaceVariant,
                modifier = Modifier.align(Alignment.TopStart).padding(4.dp))
            Text("−20 dB per line", style = label, color = colors.onSurfaceVariant,
                modifier = Modifier.align(Alignment.TopEnd).padding(4.dp))
        }
    }
}

/** Vertical grid lines, and horizontal ones every 20 dB. */
private val GRID_HZ = listOf(100f to "100 Hz", 1000f to "1 kHz", 10000f to "10 kHz")
private val GRID_DB = listOf(-20f, -40f, -60f, -80f)
