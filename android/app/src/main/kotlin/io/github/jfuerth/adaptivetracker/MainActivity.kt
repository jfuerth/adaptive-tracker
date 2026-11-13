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
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import kotlin.math.roundToInt

class MainActivity : ComponentActivity() {
    private var audioOk by mutableStateOf(true)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            AppTheme {
                DemoScreen(audioOk)
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

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun DemoScreen(audioOk: Boolean) {
    var playing by rememberSaveable { mutableStateOf(false) }
    var root by rememberSaveable { mutableIntStateOf(0) }
    var scale by rememberSaveable { mutableIntStateOf(0) }
    var tempo by rememberSaveable { mutableFloatStateOf(110f) }

    // Push the current UI state to the engine on every recomposition. The calls
    // only store values, so repeating them is harmless.
    NativeAudio.setPlaying(playing)
    NativeAudio.setKey(root, scale)
    NativeAudio.setTempo(tempo)

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
            Text(
                "Demo: one arrangement written in scale degrees. Change the key or " +
                    "scale while it plays; new notes follow the change.",
                style = MaterialTheme.typography.bodyMedium,
            )
            if (!audioOk) {
                Text(
                    "Could not open audio output.",
                    color = MaterialTheme.colorScheme.error,
                )
            }

            Button(
                onClick = { playing = !playing },
                modifier = Modifier.fillMaxWidth(),
            ) {
                Text(if (playing) "Stop" else "Play")
            }

            Text("Key", style = MaterialTheme.typography.titleMedium)
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                NativeAudio.NOTE_NAMES.forEachIndexed { i, name ->
                    FilterChip(
                        selected = root == i,
                        onClick = { root = i },
                        label = { Text(name) },
                    )
                }
            }

            Text("Scale", style = MaterialTheme.typography.titleMedium)
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                NativeAudio.Scale.entries.forEachIndexed { i, s ->
                    FilterChip(
                        selected = scale == i,
                        onClick = { scale = i },
                        label = { Text(s.label) },
                    )
                }
            }

            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Tempo", style = MaterialTheme.typography.titleMedium)
                Text(
                    "  ${tempo.roundToInt()} BPM",
                    style = MaterialTheme.typography.bodyLarge,
                )
            }
            Slider(
                value = tempo,
                onValueChange = { tempo = it.roundToInt().toFloat() },
                valueRange = 60f..200f,
            )
        }
    }
}
