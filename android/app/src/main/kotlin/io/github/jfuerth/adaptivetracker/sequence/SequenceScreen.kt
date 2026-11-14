package io.github.jfuerth.adaptivetracker.sequence

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import io.github.jfuerth.adaptivetracker.NativeAudio
import io.github.jfuerth.adaptivetracker.Song
import io.github.jfuerth.adaptivetracker.editor.NaturalOrder
import io.github.jfuerth.adaptivetracker.editor.PatternStore
import io.github.jfuerth.adaptivetracker.editor.PitchMode
import io.github.jfuerth.adaptivetracker.instrument.InstrumentStore
import io.github.jfuerth.adaptivetracker.ui.NameDialog
import io.github.jfuerth.adaptivetracker.ui.ToolButton
import kotlinx.coroutines.delay

private val LANE_WIDTH = 136.dp

/**
 * What each channel plays in a section: the channels side by side, like a
 * mixing console, each with its patterns stacked top to bottom in the order
 * they play (then looping). Sections, channels and patterns can be added here.
 */
@Composable
fun SequenceScreen(
    playing: Boolean,
    currentSection: Int,
    onOpenPattern: (Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    var section by rememberSaveable { mutableIntStateOf(0) }
    if (section >= Song.sections.size) section = 0
    if (Song.sections.isEmpty()) {
        Text("This song has no sections.", modifier.padding(16.dp))
        return
    }
    // Goes up after each edit, so the sequences are read again.
    var edits by remember { mutableIntStateOf(0) }
    val channels = Song.channels
    val sequences = remember(section, edits, channels) {
        channels.indices.map { NativeAudio.getSequence(section, it)?.toList() ?: emptyList() }
    }
    val timing = remember(section, edits) { NativeAudio.getSectionTiming(section) ?: intArrayOf(0, 4, 0) }

    // Which pattern each channel is playing now, for highlighting.
    var playingPatterns by remember { mutableStateOf(List(channels.size) { -1 }) }
    LaunchedEffect(channels.size) {
        while (true) {
            playingPatterns = List(channels.size) { NativeAudio.getPlayPosition(it)[0] }
            delay(100)
        }
    }
    val hereNow = playing && currentSection == section

    var dialog by remember { mutableStateOf<Dialog?>(null) }
    var message by remember { mutableStateOf<String?>(null) }
    fun edited() {
        edits++
        Song.structureChanged()
    }
    fun seq(channel: Int, op: Int, pos: Int, pattern: Int = 0) {
        if (NativeAudio.editSequence(section, channel, op, pos, pattern)) edits++
        else message = "That change couldn't be made (a sequence holds up to 64 patterns)."
    }

    Column(modifier.fillMaxSize()) {
        // ---- Section: choose, add, rename, timing; play it ----
        Row(Modifier.fillMaxWidth().padding(horizontal = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            var open by remember { mutableStateOf(false) }
            Box(Modifier.weight(1f)) {
                TextButton(onClick = { open = true }) {
                    Text("Section: ${Song.sections[section]} ▾", maxLines = 1, overflow = TextOverflow.Ellipsis)
                }
                DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
                    Song.sections.forEachIndexed { i, name ->
                        val bridge = Song.bridgeBeats.getOrElse(i) { 0 } > 0
                        DropdownMenuItem(
                            text = { Text((if (i == section) "✓ " else "") + name + if (bridge) " (bridge)" else "") },
                            onClick = { section = i; open = false },
                        )
                    }
                    HorizontalDivider()
                    DropdownMenuItem(text = { Text("New section…") }, onClick = { open = false; dialog = Dialog.NewSection })
                    DropdownMenuItem(text = { Text("Rename this section…") }, onClick = { open = false; dialog = Dialog.RenameSection })
                    DropdownMenuItem(text = { Text("Tempo, bar and bridge…") }, onClick = { open = false; dialog = Dialog.Timing })
                }
            }
            ToolButton(if (hereNow) "Stop" else "Play", onClick = {
                if (hereNow) NativeAudio.stop()
                else NativeAudio.gotoSection(section, NativeAudio.Quantize.NOW.ordinal, NativeAudio.Entry.RESTART.ordinal, false)
            })
        }
        Text(
            buildString {
                append(if (timing[0] == 0) "Keeps the tempo" else "%.1f BPM".format(timing[0] / 1000f).replace(".0 ", " "))
                append(" · ${timing[1]} beats per bar")
                if (timing[2] > 0) append(" · bridge: plays ${timing[2]} beats, then the next section")
            },
            style = MaterialTheme.typography.bodySmall,
            modifier = Modifier.padding(horizontal = 16.dp),
        )
        message?.let {
            Row(Modifier.padding(start = 16.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(it, color = MaterialTheme.colorScheme.error, modifier = Modifier.weight(1f))
                TextButton(onClick = { message = null }) { Text("OK") }
            }
        }

        // ---- Channels side by side; each one's patterns top to bottom ----
        Row(
            Modifier
                .weight(1f)
                .fillMaxWidth()
                .verticalScroll(rememberScrollState())
                .horizontalScroll(rememberScrollState())
                .padding(8.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            channels.forEachIndexed { ch, name ->
                Lane(
                    channel = ch,
                    name = name,
                    entries = sequences.getOrElse(ch) { emptyList() },
                    playingPattern = if (hereNow) playingPatterns.getOrElse(ch) { -1 } else -1,
                    onOpenPattern = onOpenPattern,
                    onEdit = { op, pos, pattern -> seq(ch, op, pos, pattern) },
                    onNewPattern = { pos ->
                        val p = PatternStore.create(ch, PatternStore.freshName(name))
                        if (p < 0) message = "No room for another pattern (a song holds up to 64)."
                        else seq(ch, NativeAudio.SEQ_INSERT, pos, p)
                    },
                    onRename = { dialog = Dialog.RenameChannel(ch) },
                )
            }
            if (channels.size < MAX_CHANNELS) {
                Box(Modifier.width(LANE_WIDTH)) {
                    TextButton(onClick = { dialog = Dialog.NewChannel }) { Text("+ Channel") }
                }
            }
        }
    }

    when (val d = dialog) {
        null -> {}
        Dialog.NewSection -> NameDialog("New section", "Section ${Song.sections.size + 1}", "Add") { name ->
            dialog = null
            if (name != null) {
                val i = NativeAudio.addSection(name)
                if (i < 0) message = "No room for another section (a song holds up to 16)."
                else {
                    edited()
                    section = i
                }
            }
        }
        Dialog.RenameSection -> NameDialog("Rename section", Song.sections[section], "Rename") { name ->
            dialog = null
            if (name != null && NativeAudio.rename(NativeAudio.NAMES_SECTIONS, section, name)) edited()
        }
        is Dialog.RenameChannel -> NameDialog("Rename channel", channels[d.channel], "Rename") { name ->
            dialog = null
            if (name != null && NativeAudio.rename(NativeAudio.NAMES_CHANNELS, d.channel, name)) edited()
        }
        Dialog.Timing -> TimingDialog(timing) { t ->
            dialog = null
            if (t != null && NativeAudio.setSection(section, t.bpm, t.beatsPerBar, t.bridgeBeats)) edited()
        }
        Dialog.NewChannel -> NewChannelDialog("Channel ${channels.size + 1}") { c ->
            dialog = null
            if (c != null) {
                val ch = NativeAudio.addChannel(c.name, c.mode.ordinal, c.instrument)
                if (ch < 0) {
                    message = "No room for another channel."
                } else {
                    Song.structureChanged()
                    InstrumentStore.columnsChanged()
                    // Something to play straight away: an empty pattern, in this section.
                    val p = PatternStore.create(ch, PatternStore.freshName(c.name))
                    if (p >= 0) NativeAudio.editSequence(section, ch, NativeAudio.SEQ_INSERT, 0, p)
                    edited()
                }
            }
        }
    }
}

private sealed interface Dialog {
    data object NewSection : Dialog
    data object RenameSection : Dialog
    data object Timing : Dialog
    data object NewChannel : Dialog
    data class RenameChannel(val channel: Int) : Dialog
}

/** Channels a song can have (the engine's kMaxChannels). */
private const val MAX_CHANNELS = 8

/** One channel: its name, then its patterns in the order they play, then +. */
@Composable
private fun Lane(
    channel: Int,
    name: String,
    entries: List<Int>,
    playingPattern: Int,
    onOpenPattern: (Int) -> Unit,
    onEdit: (op: Int, pos: Int, pattern: Int) -> Unit,
    onNewPattern: (pos: Int) -> Unit,
    onRename: () -> Unit,
) {
    val colors = MaterialTheme.colorScheme
    val names = PatternStore.names
    // The channel's own patterns, by name: the only ones it can play.
    val own = remember(channel, names) { PatternStore.ofChannel(channel).sortedWith(compareBy(NaturalOrder) { names[it] }) }

    Column(Modifier.width(LANE_WIDTH), verticalArrangement = Arrangement.spacedBy(4.dp)) {
        var headOpen by remember { mutableStateOf(false) }
        Box {
            Text(
                "$name ▾",
                style = MaterialTheme.typography.titleSmall,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.fillMaxWidth().clip(RoundedCornerShape(4.dp)).clickable { headOpen = true }.padding(4.dp),
            )
            DropdownMenu(expanded = headOpen, onDismissRequest = { headOpen = false }) {
                DropdownMenuItem(text = { Text("Rename channel…") }, onClick = { headOpen = false; onRename() })
            }
        }
        if (entries.isEmpty()) {
            Text("Silent in this section", style = MaterialTheme.typography.bodySmall, color = colors.onSurfaceVariant,
                modifier = Modifier.padding(4.dp))
        }
        entries.forEachIndexed { pos, pattern ->
            var open by remember { mutableStateOf(false) }
            val isPlaying = pattern == playingPattern
            Box {
                Text(
                    names.getOrElse(pattern) { "?" },
                    style = MaterialTheme.typography.bodyMedium,
                    color = if (isPlaying) colors.onPrimary else colors.onSecondaryContainer,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(8.dp))
                        .background(if (isPlaying) colors.primary else colors.secondaryContainer)
                        .clickable { open = true }
                        .padding(horizontal = 10.dp, vertical = 10.dp),
                )
                DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
                    DropdownMenuItem(text = { Text("Edit pattern") }, onClick = { open = false; onOpenPattern(pattern) })
                    DropdownMenuItem(text = { Text("Move up") }, enabled = pos > 0, onClick = {
                        open = false
                        onEdit(NativeAudio.SEQ_REMOVE, pos, 0)
                        onEdit(NativeAudio.SEQ_INSERT, pos - 1, pattern)
                    })
                    DropdownMenuItem(text = { Text("Move down") }, enabled = pos < entries.size - 1, onClick = {
                        open = false
                        onEdit(NativeAudio.SEQ_REMOVE, pos, 0)
                        onEdit(NativeAudio.SEQ_INSERT, pos + 1, pattern)
                    })
                    DropdownMenuItem(text = { Text("New pattern below") }, onClick = { open = false; onNewPattern(pos + 1) })
                    DropdownMenuItem(text = { Text("Remove from here") }, onClick = { open = false; onEdit(NativeAudio.SEQ_REMOVE, pos, 0) })
                    if (own.size > 1) {
                        HorizontalDivider()
                        Text("Play instead:", style = MaterialTheme.typography.labelMedium, modifier = Modifier.padding(horizontal = 12.dp))
                        own.filter { it != pattern }.forEach { p ->
                            DropdownMenuItem(text = { Text(names[p]) }, onClick = { open = false; onEdit(NativeAudio.SEQ_SET, pos, p) })
                        }
                    }
                }
            }
        }
        var addOpen by remember { mutableStateOf(false) }
        Box {
            TextButton(onClick = { addOpen = true }) { Text("+ Pattern") }
            DropdownMenu(expanded = addOpen, onDismissRequest = { addOpen = false }) {
                DropdownMenuItem(text = { Text("New empty pattern") }, onClick = { addOpen = false; onNewPattern(entries.size) })
                if (own.isNotEmpty()) {
                    HorizontalDivider()
                    own.forEach { p ->
                        DropdownMenuItem(text = { Text(names[p]) }, onClick = {
                            addOpen = false
                            onEdit(NativeAudio.SEQ_INSERT, entries.size, p)
                        })
                    }
                }
            }
        }
    }
}

private data class Timing(val bpm: Float, val beatsPerBar: Int, val bridgeBeats: Int)

/** A section's tempo (or none: keep the tempo), beats per bar, and whether it is a bridge. */
@Composable
private fun TimingDialog(current: IntArray, onDone: (Timing?) -> Unit) {
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

private data class NewChannel(val name: String, val mode: PitchMode, val instrument: Int)

private val MODE_LABELS = mapOf(
    PitchMode.DEGREE to "Scale degrees",
    PitchMode.TRANSPOSE to "Notes that follow the key",
    PitchMode.ABSOLUTE to "Fixed notes (drums)",
)

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun NewChannelDialog(defaultName: String, onDone: (NewChannel?) -> Unit) {
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
