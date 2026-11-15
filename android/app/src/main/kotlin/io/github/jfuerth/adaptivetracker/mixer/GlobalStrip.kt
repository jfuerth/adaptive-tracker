package io.github.jfuerth.adaptivetracker.mixer

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Card
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshots.SnapshotStateList
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import io.github.jfuerth.adaptivetracker.NativeAudio
import io.github.jfuerth.adaptivetracker.Song
import io.github.jfuerth.adaptivetracker.ui.NameDialog

/** The global strip's width when open. */
internal val GLOBAL_WIDTH = 156.dp

/**
 * The console's global section: jumping back to replay the bar or the
 * patterns, where the song goes next (sections, and how it gets there), the
 * parameters as faders, events, layers and the key.
 * [params] and [layerOn] mirror what was sent to the engine.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun GlobalStrip(
    playing: Boolean,
    currentSection: Int,
    params: SnapshotStateList<Float>,
    layerOn: SnapshotStateList<Boolean>,
    onSectionChosen: (Int) -> Unit,
    onCollapse: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val colors = MaterialTheme.colorScheme
    var quantize by rememberSaveable { mutableIntStateOf(NativeAudio.Quantize.BAR.ordinal) }
    var entry by rememberSaveable { mutableIntStateOf(NativeAudio.Entry.RESTART.ordinal) }
    var viaBridge by rememberSaveable { mutableStateOf(false) }
    // The key starts at the song's own. A scale that isn't one of the named
    // ones shows no scale selected, and changing the root keeps it.
    val home = remember { NativeAudio.getHomeKey() }
    var root by rememberSaveable { mutableIntStateOf(home[0]) }
    var scaleMask by rememberSaveable { mutableIntStateOf(home[1]) }
    var dialog by remember { mutableStateOf<GlobalDialog?>(null) }

    Column(
        modifier
            .width(GLOBAL_WIDTH)
            .fillMaxHeight()
            .clip(RoundedCornerShape(8.dp))
            .background(colors.surfaceContainerHigh)
            .verticalScroll(rememberScrollState())
            .padding(6.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("Global", style = MaterialTheme.typography.titleSmall, modifier = Modifier.weight(1f))
            Text(
                "▶",
                modifier = Modifier.clip(RoundedCornerShape(4.dp)).clickable(onClick = onCollapse).padding(6.dp),
            )
        }

        // Bump: everything jumps back, at once, to replay the bar or the patterns.
        Heading("Back to start of")
        Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            Box(Modifier.weight(1f)) {
                StripChip("⏮ Bar", on = false, enabled = playing, onClick = { NativeAudio.rewind(toPattern = false) })
            }
            Box(Modifier.weight(1f)) {
                StripChip("⏮ Pattern", on = false, enabled = playing, onClick = { NativeAudio.rewind(toPattern = true) })
            }
        }

        // Sections: go to one (while playing, as the transition says).
        Heading("Go to")
        Song.playableSections.forEach { i ->
            val here = playing && i == currentSection
            StripChip(Song.sections[i], on = here, onClick = {
                if (playing) NativeAudio.gotoSection(i, quantize, entry, viaBridge)
                onSectionChosen(i)
            })
        }
        Text(
            buildString {
                append(NativeAudio.Quantize.entries[quantize].label)
                append(" · ").append(NativeAudio.Entry.entries[entry].label)
                if (viaBridge) append(" · via bridge")
                append(" ✎")
            },
            style = MaterialTheme.typography.bodySmall,
            color = colors.primary,
            modifier = Modifier.clip(RoundedCornerShape(4.dp)).clickable { dialog = GlobalDialog.Transition }.padding(4.dp),
        )

        // Parameters, as faders side by side.
        if (Song.params.isNotEmpty()) {
            Heading("Parameters")
            Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                Song.params.forEachIndexed { i, name ->
                    Column(Modifier.width(44.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                        Text("${(params.getOrElse(i) { 0f } * 100).toInt()}%", style = MaterialTheme.typography.labelSmall)
                        VerticalFader(
                            value = params.getOrElse(i) { 0f },
                            onValue = {
                                if (i < params.size) params[i] = it
                                NativeAudio.setParam(i, it)
                            },
                            modifier = Modifier.width(36.dp).height(140.dp),
                        )
                        Text(
                            name,
                            style = MaterialTheme.typography.labelSmall,
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis,
                            textAlign = TextAlign.Center,
                        )
                    }
                }
            }
        }

        if (Song.events.isNotEmpty()) {
            Heading("Events")
            Song.events.forEachIndexed { i, name ->
                StripChip(name, on = false, enabled = playing, onClick = { NativeAudio.fireEvent(i) })
            }
        }

        Row(verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.weight(1f)) { Heading("Layers") }
            Text(
                "Edit",
                style = MaterialTheme.typography.labelMedium,
                color = colors.primary,
                modifier = Modifier.clip(RoundedCornerShape(4.dp)).clickable { dialog = GlobalDialog.Layers }.padding(4.dp),
            )
        }
        if (Song.layers.isEmpty()) {
            Text("None yet", style = MaterialTheme.typography.bodySmall, color = colors.onSurfaceVariant)
        }
        Song.layers.forEachIndexed { i, name ->
            StripChip(name, on = layerOn.getOrElse(i) { true }, onClick = {
                if (i < layerOn.size) {
                    layerOn[i] = !layerOn[i]
                    NativeAudio.setLayerMute(i, !layerOn[i])
                }
            })
        }
        Text("Layers mute at the next bar.", style = MaterialTheme.typography.labelSmall, color = colors.onSurfaceVariant)

        Heading("Key")
        val scaleName = NativeAudio.Scale.entries.firstOrNull { it.mask == scaleMask }?.label ?: "own scale"
        Text(
            "${NativeAudio.NOTE_NAMES[root]} $scaleName ✎",
            style = MaterialTheme.typography.bodyMedium,
            color = colors.primary,
            modifier = Modifier.clip(RoundedCornerShape(4.dp)).clickable { dialog = GlobalDialog.Key }.padding(4.dp),
        )
        Spacer(Modifier.height(8.dp))
    }

    when (dialog) {
        null -> {}
        GlobalDialog.Transition -> AlertDialog(
            onDismissRequest = { dialog = null },
            title = { Text("Going to a section") },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text("When", style = MaterialTheme.typography.labelLarge)
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        NativeAudio.Quantize.entries.forEach { q ->
                            FilterChip(selected = quantize == q.ordinal, onClick = { quantize = q.ordinal }, label = { Text(q.label) })
                        }
                    }
                    Text("Where it starts", style = MaterialTheme.typography.labelLarge)
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        NativeAudio.Entry.entries.forEach { e ->
                            FilterChip(selected = entry == e.ordinal, onClick = { entry = e.ordinal }, label = { Text(e.label) })
                        }
                    }
                    if (Song.hasBridge) Row(verticalAlignment = Alignment.CenterVertically) {
                        Switch(checked = viaBridge, onCheckedChange = { viaBridge = it })
                        Text("  Play the bridge first")
                    }
                }
            },
            confirmButton = { TextButton(onClick = { dialog = null }) { Text("Done") } },
        )
        GlobalDialog.Key -> AlertDialog(
            onDismissRequest = { dialog = null },
            title = { Text("Key (changes at the next bar)") },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                        NativeAudio.NOTE_NAMES.forEachIndexed { i, name ->
                            FilterChip(selected = root == i, onClick = {
                                root = i
                                NativeAudio.setKey(root, scaleMask)
                            }, label = { Text(name) })
                        }
                    }
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                        NativeAudio.Scale.entries.forEach { s ->
                            FilterChip(selected = scaleMask == s.mask, onClick = {
                                scaleMask = s.mask
                                NativeAudio.setKey(root, scaleMask)
                            }, label = { Text(s.label) })
                        }
                    }
                }
            },
            confirmButton = { TextButton(onClick = { dialog = null }) { Text("Done") } },
        )
        GlobalDialog.Layers -> AlertDialog(
            onDismissRequest = { dialog = null },
            title = { Text("Layers") },
            text = {
                Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    LayerEditor(layerOn)
                }
            },
            confirmButton = { TextButton(onClick = { dialog = null }) { Text("Done") } },
        )
    }
}

private enum class GlobalDialog { Transition, Key, Layers }

@Composable
private fun Heading(text: String) {
    Text(text, style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.onSurfaceVariant)
}

/** A full-width toggle-looking button for the strips. */
@Composable
internal fun StripChip(text: String, on: Boolean, onClick: () -> Unit, enabled: Boolean = true) {
    val colors = MaterialTheme.colorScheme
    Text(
        text,
        style = MaterialTheme.typography.labelLarge,
        color = when {
            !enabled -> colors.onSurface.copy(alpha = 0.38f)
            on -> colors.onPrimary
            else -> colors.onSecondaryContainer
        },
        maxLines = 1,
        overflow = TextOverflow.Ellipsis,
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(6.dp))
            .background(if (on) colors.primary else colors.secondaryContainer)
            .clickable(enabled = enabled, onClick = onClick)
            .padding(horizontal = 8.dp, vertical = 8.dp),
    )
}

/**
 * Layers being edited: each with its name (tap to rename), its channels (tap
 * one to put all its columns in or out; single columns are chosen in a
 * column's settings), and Remove. [layerOn] follows additions and removals.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun LayerEditor(layerOn: SnapshotStateList<Boolean>) {
    var renaming by remember { mutableStateOf<Int?>(null) }
    var removing by remember { mutableStateOf<Int?>(null) }
    var adding by remember { mutableStateOf(false) }
    Song.layers.forEachIndexed { i, name ->
        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    TextButton(onClick = { renaming = i }) { Text("$name ✎") }
                    Spacer(Modifier.weight(1f))
                    TextButton(onClick = { removing = i }) { Text("Remove") }
                }
                FlowRow(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    Song.channels.forEachIndexed { ch, chName ->
                        val bits = Song.layerColumns(i, ch)
                        val all = (1 shl NativeAudio.getChannelColumns(ch)) - 1
                        val whole = bits and all == all
                        FilterChip(
                            selected = bits != 0,
                            onClick = {
                                NativeAudio.setLayerColumns(i, ch, if (whole) 0 else 0xF)
                                Song.structureChanged()
                            },
                            label = { Text(if (bits != 0 && !whole) "$chName (some columns)" else chName) },
                        )
                    }
                }
            }
        }
    }
    TextButton(onClick = { adding = true }, enabled = Song.layers.size < MAX_LAYERS) { Text("+ New layer") }
    Text(
        "To put single columns in a layer, use a column's settings in the Patterns tab (tap its heading).",
        style = MaterialTheme.typography.bodySmall,
    )

    if (adding) NameDialog("New layer", "Layer ${Song.layers.size + 1}", "Add") { name ->
        adding = false
        if (name != null && NativeAudio.addLayer(name) >= 0) {
            layerOn.add(true)
            Song.structureChanged()
        }
    }
    renaming?.let { i ->
        NameDialog("Rename layer", Song.layers[i], "Rename") { name ->
            renaming = null
            if (name != null && NativeAudio.rename(NativeAudio.NAMES_LAYERS, i, name)) Song.structureChanged()
        }
    }
    removing?.let { i ->
        AlertDialog(
            onDismissRequest = { removing = null },
            title = { Text("Remove “${Song.layers[i]}”?") },
            text = { Text("Its columns play as they would without it. Mappings and event actions that use it are removed too.") },
            confirmButton = {
                TextButton(onClick = {
                    removing = null
                    if (NativeAudio.removeLayer(i)) {
                        if (i < layerOn.size) layerOn.removeAt(i)
                        Song.structureChanged()
                    }
                }) { Text("Remove") }
            },
            dismissButton = { TextButton(onClick = { removing = null }) { Text("Cancel") } },
        )
    }
}

/** Layers a song can have (the engine's kMaxLayers). */
private const val MAX_LAYERS = 8
