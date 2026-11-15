package io.github.jfuerth.adaptivetracker.editor

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Checkbox
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import io.github.jfuerth.adaptivetracker.NativeAudio
import io.github.jfuerth.adaptivetracker.Song
import kotlin.math.roundToInt

/** How notes can be written, as the dialogs say it. */
val PITCH_MODE_LABELS = listOf(
    PitchMode.DEGREE to "Scale degrees",
    PitchMode.TRANSPOSE to "Notes that follow the key",
    PitchMode.ABSOLUTE to "Fixed notes",
)

/**
 * A column's settings: how its notes are written, the parameter level it
 * plays from, and the layers it is in. Changes apply on Set.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun ColumnSettingsDialog(channel: Int, column: Int, channelName: String, onDone: () -> Unit) {
    val settings = remember { NativeAudio.getColumnSettings(channel) }
    val startMode = PitchMode.entries[settings.getOrElse(column * 3) { 0 }]
    val startParam = settings.getOrElse(column * 3 + 1) { -1 }
    val startThreshold = settings.getOrElse(column * 3 + 2) { 500 } / 1000f
    val startLayers = remember { Song.layers.indices.filter { Song.layerColumns(it, channel) and (1 shl column) != 0 }.toSet() }

    var mode by remember { mutableStateOf(startMode) }
    var param by remember { mutableIntStateOf(startParam) }
    var threshold by remember { mutableFloatStateOf(if (startParam >= 0) startThreshold else 0.5f) }
    var layers by remember { mutableStateOf(startLayers) }

    AlertDialog(
        onDismissRequest = onDone,
        title = { Text("$channelName · column ${column + 1}") },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("Notes are written as", style = MaterialTheme.typography.labelLarge)
                FlowRow(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    PITCH_MODE_LABELS.forEach { (m, label) ->
                        FilterChip(selected = mode == m, onClick = { mode = m }, label = { Text(label) })
                    }
                }
                if (mode != startMode) {
                    Text(
                        "The column's notes, in every $channelName pattern, are rewritten to sound the same " +
                            "in the song's key, arpeggios included.",
                        style = MaterialTheme.typography.bodySmall,
                    )
                }

                Text("Plays", style = MaterialTheme.typography.labelLarge)
                FlowRow(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    FilterChip(selected = param < 0, onClick = { param = -1 }, label = { Text("Always") })
                    Song.params.forEachIndexed { i, name ->
                        FilterChip(selected = param == i, onClick = { param = i }, label = { Text("By $name") })
                    }
                }
                if (param >= 0) {
                    val name = Song.params.getOrElse(param) { "?" }
                    Text("When $name is at least ${(threshold * 100).roundToInt()}%", style = MaterialTheme.typography.bodyMedium)
                    Slider(value = threshold, onValueChange = { threshold = (it * 100).roundToInt() / 100f })
                    Text(
                        "Below that, the column starts no new notes; what's sounding ends at its next note. " +
                            "Played alone in this editor, it always plays.",
                        style = MaterialTheme.typography.bodySmall,
                    )
                }

                Text("Layers", style = MaterialTheme.typography.labelLarge)
                if (Song.layers.isEmpty()) {
                    Text("No layers yet. Make them on the Perform tab, with Edit by Layers.", style = MaterialTheme.typography.bodySmall)
                }
                Song.layers.forEachIndexed { i, name ->
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Checkbox(checked = i in layers, onCheckedChange = { on -> layers = if (on) layers + i else layers - i })
                        Text(name)
                    }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = {
                if (mode != startMode) PatternStore.setColumnPitch(channel, column, mode)
                if (param != startParam || (param >= 0 && threshold != startThreshold)) {
                    NativeAudio.setColumnGate(channel, column, param, if (param >= 0) threshold else 0f)
                }
                if (layers != startLayers) {
                    for (l in Song.layers.indices) {
                        val bits = Song.layerColumns(l, channel)
                        val want = if (l in layers) bits or (1 shl column) else bits and (1 shl column).inv()
                        if (want != bits) NativeAudio.setLayerColumns(l, channel, want)
                    }
                    Song.structureChanged()
                }
                onDone()
            }) { Text("Set") }
        },
        dismissButton = { TextButton(onClick = onDone) { Text("Cancel") } },
    )
}
