package io.github.jfuerth.adaptivetracker.mixer

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import io.github.jfuerth.adaptivetracker.editor.PitchMode
import io.github.jfuerth.adaptivetracker.instrument.InstrumentStore

// Dialogs for sections and channels, used by the mixer.

internal data class Timing(val bpm: Float, val beatsPerBar: Int, val bridgeBeats: Int)

/** A section's tempo (or none: keep the tempo), beats per bar, and whether it is a bridge. */
@Composable
internal fun TimingDialog(current: IntArray, onDone: (Timing?) -> Unit) {
    var setsTempo by remember { mutableStateOf(current[0] != 0) }
    var bpmText by remember { mutableStateOf(if (current[0] != 0) "%.0f".format(current[0] / 1000f) else "120") }
    var barText by remember { mutableStateOf(current[1].toString()) }
    var isBridge by remember { mutableStateOf(current[2] > 0) }
    var bridgeText by remember { mutableStateOf(if (current[2] > 0) current[2].toString() else "8") }
    val bpm = bpmText.toFloatOrNull()?.takeIf { it in 20f..400f }
    val bar = barText.toIntOrNull()?.takeIf { it in 1..32 }
    val bridge = bridgeText.toIntOrNull()?.takeIf { it in 1..999 }
    val ok = bar != null && (!setsTempo || bpm != null) && (!isBridge || bridge != null)

    AlertDialog(
        onDismissRequest = { onDone(null) },
        title = { Text("Tempo, bar and bridge") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Switch(checked = setsTempo, onCheckedChange = { setsTempo = it })
                    Text("  Set the tempo on entry", Modifier.weight(1f))
                }
                if (setsTempo) NumberField("BPM (20–400)", bpmText, bpm == null) { bpmText = it }
                NumberField("Beats per bar (1–32)", barText, bar == null) { barText = it }
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Switch(checked = isBridge, onCheckedChange = { isBridge = it })
                    Text("  A bridge: plays on the way to another section", Modifier.weight(1f))
                }
                if (isBridge) NumberField("Beats it plays", bridgeText, bridge == null) { bridgeText = it }
            }
        },
        confirmButton = {
            TextButton(enabled = ok, onClick = {
                onDone(Timing(if (setsTempo) bpm ?: 0f else 0f, bar ?: 4, if (isBridge) bridge ?: 0 else 0))
            }) { Text("Set") }
        },
        dismissButton = { TextButton(onClick = { onDone(null) }) { Text("Cancel") } },
    )
}

@Composable
private fun NumberField(label: String, text: String, isError: Boolean, onText: (String) -> Unit) {
    OutlinedTextField(
        text,
        onValueChange = { t -> onText(t.filter { it.isDigit() || it == '.' }.take(6)) },
        label = { Text(label) },
        singleLine = true,
        isError = isError,
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
    )
}

internal data class NewChannel(val name: String, val mode: PitchMode, val instrument: Int)

private val MODE_LABELS = mapOf(
    PitchMode.DEGREE to "Scale degrees",
    PitchMode.TRANSPOSE to "Notes that follow the key",
    PitchMode.ABSOLUTE to "Fixed notes (drums)",
)

@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun NewChannelDialog(defaultName: String, onDone: (NewChannel?) -> Unit) {
    var name by remember { mutableStateOf(defaultName) }
    var mode by remember { mutableStateOf(PitchMode.DEGREE) }
    var instrument by remember { mutableIntStateOf(if (InstrumentStore.names.isEmpty()) -1 else 0) }
    AlertDialog(
        onDismissRequest = { onDone(null) },
        title = { Text("New channel") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(name, onValueChange = { name = it }, label = { Text("Name") }, singleLine = true)
                Text("Notes are written as", style = MaterialTheme.typography.labelLarge)
                FlowRow(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    MODE_LABELS.forEach { (m, label) ->
                        FilterChip(selected = mode == m, onClick = { mode = m }, label = { Text(label) })
                    }
                }
                Text("Instrument", style = MaterialTheme.typography.labelLarge)
                FlowRow(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    InstrumentStore.names.forEachIndexed { i, n ->
                        FilterChip(selected = instrument == i, onClick = { instrument = i }, label = { Text(n) })
                    }
                }
            }
        },
        confirmButton = {
            TextButton(enabled = name.isNotBlank(), onClick = { onDone(NewChannel(name.trim(), mode, instrument)) }) {
                Text("Add")
            }
        },
        dismissButton = { TextButton(onClick = { onDone(null) }) { Text("Cancel") } },
    )
}
