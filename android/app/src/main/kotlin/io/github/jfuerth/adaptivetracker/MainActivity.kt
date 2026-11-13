package io.github.jfuerth.adaptivetracker

import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.delay

class MainActivity : ComponentActivity() {
    private var audioOk by mutableStateOf(true)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            AppTheme {
                AdaptiveScreen(audioOk)
            }
        }
    }

    override fun onStart() {
        super.onStart()
        audioOk = NativeAudio.open()
    }

    override fun onStop() {
        // No background playback yet (that needs a foreground service).
        NativeAudio.close()
        super.onStop()
    }
}

@Composable
private fun AppTheme(content: @Composable () -> Unit) {
    val dark = isSystemInDarkTheme()
    val colors = when {
        Build.VERSION.SDK_INT >= Build.VERSION_CODES.S -> {
            val context = LocalContext.current
            if (dark) dynamicDarkColorScheme(context) else dynamicLightColorScheme(context)
        }
        dark -> darkColorScheme()
        else -> lightColorScheme()
    }
    MaterialTheme(colorScheme = colors, content = content)
}

/** Polls the engine's state and health twice a second. */
@Composable
private fun rememberStats(): IntArray {
    var stats by remember { mutableStateOf(IntArray(6) { if (it == 5) -1 else 0 }) }
    LaunchedEffect(Unit) {
        while (true) {
            stats = NativeAudio.getStats()
            delay(250)
        }
    }
    return stats
}

@Composable
private fun SectionTitle(text: String) {
    Text(text, style = MaterialTheme.typography.titleMedium)
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun AdaptiveScreen(audioOk: Boolean) {
    val stats = rememberStats()
    val currentSection = stats.getOrElse(5) { -1 }
    val playing = currentSection >= 0

    val params = remember { mutableStateListOf(0.2f, 0f) }
    val layerOn = remember { mutableStateListOf(true, true, true) }
    var quantize by rememberSaveable { mutableIntStateOf(NativeAudio.Quantize.BAR.ordinal) }
    var entry by rememberSaveable { mutableIntStateOf(NativeAudio.Entry.RESTART.ordinal) }
    var viaBridge by rememberSaveable { mutableStateOf(false) }
    var root by rememberSaveable { mutableIntStateOf(0) }
    var scale by rememberSaveable { mutableIntStateOf(0) }

    // Send the starting parameter values once.
    LaunchedEffect(Unit) {
        params.forEachIndexed { i, v -> NativeAudio.setParam(i, v) }
    }

    Scaffold { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(horizontal = 16.dp, vertical = 8.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            Text("Adaptive Tracker", style = MaterialTheme.typography.headlineMedium)
            if (!audioOk) {
                Text("Could not open audio output.", color = MaterialTheme.colorScheme.error)
            }

            // Transport
            Row(verticalAlignment = Alignment.CenterVertically) {
                Button(onClick = { if (playing) NativeAudio.stop() else NativeAudio.play() }) {
                    Text(if (playing) "Stop" else "Play")
                }
                Text(
                    if (playing) "  Now playing: ${NativeAudio.SECTION_NAMES.getOrElse(currentSection) { "?" }}"
                    else "  Stopped",
                    style = MaterialTheme.typography.bodyLarge,
                )
            }

            // Parameters
            SectionTitle("Parameters")
            Text(
                "Intensity brings in the rhythm layer, brightens the arpeggio and pushes the tempo. " +
                    "Danger muffles the pad; past 75% it starts combat, and below it calms down.",
                style = MaterialTheme.typography.bodySmall,
            )
            NativeAudio.PARAMS.forEachIndexed { i, name ->
                Text("$name: ${(params[i] * 100).toInt()}%")
                Slider(
                    value = params[i],
                    onValueChange = {
                        params[i] = it
                        NativeAudio.setParam(i, it)
                    },
                )
            }

            // Events
            SectionTitle("Events")
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                NativeAudio.EVENT_BUTTONS.forEach { (index, label) ->
                    FilledTonalButton(onClick = { NativeAudio.fireEvent(index) }, enabled = playing) {
                        Text(label)
                    }
                }
            }

            // Sections
            SectionTitle("Sections")
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                NativeAudio.SECTIONS.forEachIndexed { i, name ->
                    FilledTonalButton(
                        onClick = { NativeAudio.gotoSection(i, quantize, entry, viaBridge) },
                        enabled = playing,
                    ) { Text("Go to $name") }
                }
            }
            Text("Transition", style = MaterialTheme.typography.labelLarge)
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                NativeAudio.Quantize.entries.forEach { q ->
                    FilterChip(
                        selected = quantize == q.ordinal,
                        onClick = { quantize = q.ordinal },
                        label = { Text(q.label) },
                    )
                }
            }
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                NativeAudio.Entry.entries.forEach { e ->
                    FilterChip(
                        selected = entry == e.ordinal,
                        onClick = { entry = e.ordinal },
                        label = { Text(e.label) },
                    )
                }
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Switch(checked = viaBridge, onCheckedChange = { viaBridge = it })
                Text("  Play the bridge first")
            }

            // Layers
            SectionTitle("Layers (mute at the next bar)")
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                NativeAudio.LAYERS.forEachIndexed { i, name ->
                    FilterChip(
                        selected = layerOn[i],
                        onClick = {
                            layerOn[i] = !layerOn[i]
                            NativeAudio.setLayerMute(i, !layerOn[i])
                        },
                        label = { Text(name) },
                    )
                }
            }

            // Key
            SectionTitle("Key (changes at the next bar)")
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                NativeAudio.NOTE_NAMES.forEachIndexed { i, name ->
                    FilterChip(
                        selected = root == i,
                        onClick = {
                            root = i
                            NativeAudio.setKey(root, scale)
                        },
                        label = { Text(name) },
                    )
                }
            }
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                NativeAudio.Scale.entries.forEachIndexed { i, s ->
                    FilterChip(
                        selected = scale == i,
                        onClick = {
                            scale = i
                            NativeAudio.setKey(root, scale)
                        },
                        label = { Text(s.label) },
                    )
                }
            }

            DiagnosticsCard(stats)
        }
    }
}

@Composable
private fun DiagnosticsCard(stats: IntArray) {
    val underruns = stats.getOrElse(0) { 0 }
    val bufferFrames = stats.getOrElse(1) { 0 }
    val burstFrames = stats.getOrElse(2) { 0 }
    val maxUs = stats.getOrElse(3) { 0 }
    val budgetUs = stats.getOrElse(4) { 0 }
    val bufferMs = bufferFrames * 1000f / 48000f

    Card(modifier = Modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            Text("Diagnostics", style = MaterialTheme.typography.titleMedium)
            Text("Underruns: $underruns")
            Text("Buffer: $bufferFrames frames (%.1f ms), burst $burstFrames".format(bufferMs))
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Slowest callback: $maxUs µs of $budgetUs µs")
                Spacer(Modifier.width(8.dp))
                OutlinedButton(onClick = { NativeAudio.resetStats() }) { Text("Reset") }
            }
        }
    }
}
