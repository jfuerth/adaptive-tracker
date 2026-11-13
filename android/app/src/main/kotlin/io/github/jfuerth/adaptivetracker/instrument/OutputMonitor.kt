package io.github.jfuerth.adaptivetracker.instrument

import android.graphics.Bitmap
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
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import io.github.jfuerth.adaptivetracker.NativeAudio

private const val SAMPLE_RATE = 48000f
private const val FFT_SIZE = 2048
/** The oscilloscope shows this many samples (32 ms). */
private const val SCOPE_WINDOW = 1536
/** How far back the scope looks for a rising zero crossing: periods down to ~40 Hz. */
private const val SCOPE_SEARCH = 1200
private const val READ_SIZE = SCOPE_WINDOW + SCOPE_SEARCH + 1

/** Spectrogram history: one column per frame, about 2.5 s at 60 fps. */
private const val SPECTRO_COLUMNS = 150
private const val SPECTRO_BANDS = 96
private const val MIN_HZ = 30f
private const val MAX_HZ = 16000f
private const val FLOOR_DB = -90f

/**
 * Live view of one instrument's output: an oscilloscope over a scrolling
 * spectrogram. Everything the instrument plays is shown (keyboard previews
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
    val palette = remember(background) { heatPalette(background) }
    val pixels = remember { IntArray(SPECTRO_COLUMNS * SPECTRO_BANDS) }
    val bitmap = remember { Bitmap.createBitmap(SPECTRO_COLUMNS, SPECTRO_BANDS, Bitmap.Config.ARGB_8888) }
    val image = remember(bitmap) { bitmap.asImageBitmap() }
    var scope by remember { mutableStateOf(FloatArray(0)) }
    var frame by remember { mutableIntStateOf(0) }  // redraws the spectrogram

    LaunchedEffect(palette) {
        pixels.fill(palette[0])
        var last = 0L
        while (true) {
            val now = withFrameNanos { it }
            if (now - last < 15_000_000L) continue  // ~60 columns per second, whatever the display rate
            last = now
            val samples = NativeAudio.readMonitor(READ_SIZE) ?: continue
            val start = Analysis.triggerStart(samples, SCOPE_WINDOW, SCOPE_SEARCH)
            scope = samples.copyOfRange(start, start + SCOPE_WINDOW)

            val bands = Analysis.logBands(
                Analysis.spectrum(samples.copyOfRange(samples.size - FFT_SIZE, samples.size)),
                SAMPLE_RATE, SPECTRO_BANDS, MIN_HZ, MAX_HZ,
            )
            // Scroll left one column (each row's first pixel lands at the end
            // of the row above, which the new column then overwrites).
            System.arraycopy(pixels, 1, pixels, 0, pixels.size - 1)
            for (b in 0 until SPECTRO_BANDS) {
                val t = ((bands[b] - FLOOR_DB) / -FLOOR_DB).coerceIn(0f, 1f)
                val row = SPECTRO_BANDS - 1 - b  // low frequencies at the bottom
                pixels[row * SPECTRO_COLUMNS + SPECTRO_COLUMNS - 1] = palette[(t * (palette.size - 1)).toInt()]
            }
            bitmap.setPixels(pixels, 0, SPECTRO_COLUMNS, 0, 0, SPECTRO_COLUMNS, SPECTRO_BANDS)
            frame++
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
            Canvas(Modifier.fillMaxWidth().height(96.dp)) {
                frame  // read so each new column redraws
                drawImage(
                    image,
                    srcOffset = IntOffset.Zero,
                    srcSize = IntSize(SPECTRO_COLUMNS, SPECTRO_BANDS),
                    dstSize = IntSize(size.width.toInt(), size.height.toInt()),
                    filterQuality = FilterQuality.Low,
                )
            }
            Text("16 kHz", style = label, color = colors.onSurfaceVariant,
                modifier = Modifier.align(Alignment.TopStart).padding(4.dp))
            Text("30 Hz", style = label, color = colors.onSurfaceVariant,
                modifier = Modifier.align(Alignment.BottomStart).padding(4.dp))
        }
    }
}

/** 256 colours from [background] (silence) through violet and orange to pale yellow (0 dB). */
private fun heatPalette(background: Color): IntArray {
    val stops = listOf(
        background,
        Color(0xFF3B0F70),
        Color(0xFF8C2981),
        Color(0xFFDE4968),
        Color(0xFFFE9F6D),
        Color(0xFFFCFDBF),
    )
    return IntArray(256) { i ->
        val x = i / 255f * (stops.size - 1)
        val k = x.toInt().coerceAtMost(stops.size - 2)
        lerp(stops[k], stops[k + 1], x - k).toArgb()
    }
}
