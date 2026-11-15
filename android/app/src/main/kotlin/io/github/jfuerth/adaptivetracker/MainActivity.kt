package io.github.jfuerth.adaptivetracker

import android.content.Intent
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
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
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.PrimaryTabRow
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Tab
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.saveable.rememberSaveableStateHolder
import androidx.compose.runtime.setValue
import androidx.compose.runtime.toMutableStateList
import androidx.compose.runtime.snapshots.SnapshotStateList
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import io.github.jfuerth.adaptivetracker.editor.EditorScreen
import io.github.jfuerth.adaptivetracker.instrument.InstrumentScreen
import io.github.jfuerth.adaptivetracker.sequence.SequenceScreen
import io.github.jfuerth.adaptivetracker.ui.NameDialog
import kotlinx.coroutines.delay

class MainActivity : ComponentActivity() {
    private var audioOk by mutableStateOf(true)

    private var crashReport by mutableStateOf<String?>(null)
    private var songError by mutableStateOf<String?>(null)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        CrashReports.install(this)
        crashReport = CrashReports.pending(this)
        // Once per process: the engine keeps its song across activity restarts.
        if (!Song.isLoaded) songError = Song.start(this)
        enableEdgeToEdge()
        setContent {
            AppTheme {
                App(
                    audioOk = audioOk,
                    songError = songError,
                    onDismissSongError = { songError = null },
                    crashReport = crashReport,
                    onShareCrash = ::shareCrashReport,
                    onDismissCrash = {
                        CrashReports.dismiss(this)
                        crashReport = null
                    },
                )
            }
        }
    }

    /** Hands the report to any app that takes text (a chat, email, notes). */
    private fun shareCrashReport() {
        val report = crashReport ?: return
        val send = Intent(Intent.ACTION_SEND).apply {
            type = "text/plain"
            putExtra(Intent.EXTRA_SUBJECT, "Adaptive Tracker crash report")
            putExtra(Intent.EXTRA_TEXT, report)
        }
        startActivity(Intent.createChooser(send, "Share crash report"))
    }

    override fun onStart() {
        super.onStart()
        audioOk = NativeAudio.open()
    }

    override fun onStop() {
        Song.autosave(this)
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
    var stats by remember { mutableStateOf(IntArray(7) { if (it == 5) -1 else 0 }) }
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

@Composable
private fun App(
    audioOk: Boolean,
    songError: String?,
    onDismissSongError: () -> Unit,
    crashReport: String?,
    onShareCrash: () -> Unit,
    onDismissCrash: () -> Unit,
) {
    val context = LocalContext.current
    val stats = rememberStats()
    val playing = stats.getOrElse(6) { 0 } == 1
    var tab by rememberSaveable { mutableIntStateOf(0) }
    // Saves a few seconds after edits, so a crash loses little.
    LaunchedEffect(Unit) {
        while (true) {
            delay(5000)
            Song.autosave(context)
        }
    }

    Scaffold { padding ->
        Column(Modifier.fillMaxSize().padding(padding)) {
            if (crashReport != null) CrashCard(crashReport, onShareCrash, onDismissCrash)
            if (songError != null) {
                Row(Modifier.padding(start = 16.dp), verticalAlignment = Alignment.CenterVertically) {
                    Text(songError, color = MaterialTheme.colorScheme.error, modifier = Modifier.weight(1f))
                    TextButton(onClick = onDismissSongError) { Text("OK") }
                }
            }
            SongBar()
            PrimaryTabRow(selectedTabIndex = tab) {
                TABS.forEachIndexed { i, name ->
                    Tab(selected = tab == i, onClick = {
                        // The Perform tab always hears the adaptive mix.
                        if (i == 0 && tab != 0) NativeAudio.setHearAll(false)
                        tab = i
                    }, text = { Text(name, maxLines = 1) })
                }
            }
            // A new song starts every screen afresh.
            key(Song.generation) {
                // Held here so they survive switching tabs. Parameters start where the song says.
                val params = remember { Song.paramInitial.toMutableStateList() }
                val layerOn = remember { Song.layers.map { true }.toMutableStateList() }
                // Each tab keeps its state (choices, scroll positions) while another is shown.
                val tabState = rememberSaveableStateHolder()
                tabState.SaveableStateProvider(tab) {
                    when (tab) {
                        0 -> PerformScreen(audioOk, stats, params, layerOn, Modifier.weight(1f))
                        1 -> SequenceScreen(
                            playing = playing,
                            currentSection = stats.getOrElse(5) { -1 },
                            onOpenPattern = { p ->
                                Song.pattern = p
                                tab = 2
                            },
                            modifier = Modifier.weight(1f),
                        )
                        2 -> EditorScreen(playing = playing, modifier = Modifier.weight(1f))
                        else -> InstrumentScreen(Modifier.weight(1f))
                    }
                }
            }
        }
    }
}

private val TABS = listOf("Perform", "Sequence", "Patterns", "Instruments")

/** The song's title, and a menu to rename it, open and export songs. */
@Composable
private fun SongBar() {
    val context = LocalContext.current
    var menuOpen by remember { mutableStateOf(false) }
    var renaming by remember { mutableStateOf(false) }
    /** What a confirmed replace loads. */
    var confirmReplace by remember { mutableStateOf<Replace?>(null) }
    var message by remember { mutableStateOf<String?>(null) }

    val openFile = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) message = Song.import(context, uri)?.let { "Couldn't open that song: $it" }
    }
    val exportFile = rememberLauncherForActivityResult(
        ActivityResultContracts.CreateDocument("application/json"),
    ) { uri ->
        if (uri != null) message = Song.export(context, uri) ?: "Exported."
    }

    Row(Modifier.fillMaxWidth().padding(start = 16.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(
            Song.title.ifEmpty { "Untitled" },
            style = MaterialTheme.typography.titleMedium,
            maxLines = 1,
            modifier = Modifier.weight(1f),
        )
        Box {
            TextButton(onClick = { menuOpen = true }) { Text("Song ▾") }
            DropdownMenu(expanded = menuOpen, onDismissRequest = { menuOpen = false }) {
                DropdownMenuItem(text = { Text("Rename song…") }, onClick = {
                    menuOpen = false
                    renaming = true
                })
                DropdownMenuItem(text = { Text("Export to a file…") }, onClick = {
                    menuOpen = false
                    exportFile.launch(Song.exportName)
                })
                DropdownMenuItem(text = { Text("New song") }, onClick = {
                    menuOpen = false
                    confirmReplace = Replace.NEW
                })
                DropdownMenuItem(text = { Text("Open a song file…") }, onClick = {
                    menuOpen = false
                    confirmReplace = Replace.FILE
                })
                DropdownMenuItem(text = { Text("Start from the demo song") }, onClick = {
                    menuOpen = false
                    confirmReplace = Replace.DEMO
                })
            }
        }
    }
    message?.let {
        Row(Modifier.padding(start = 16.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(it, style = MaterialTheme.typography.bodyMedium, modifier = Modifier.weight(1f))
            TextButton(onClick = { message = null }) { Text("OK") }
        }
    }

    if (renaming) {
        var text by remember { mutableStateOf(Song.title) }
        AlertDialog(
            onDismissRequest = { renaming = false },
            title = { Text("Rename song") },
            text = { OutlinedTextField(text, onValueChange = { text = it }, singleLine = true) },
            confirmButton = {
                TextButton(onClick = {
                    Song.rename(text)
                    renaming = false
                }, enabled = text.isNotBlank()) { Text("Rename") }
            },
            dismissButton = { TextButton(onClick = { renaming = false }) { Text("Cancel") } },
        )
    }

    confirmReplace?.let { what ->
        AlertDialog(
            onDismissRequest = { confirmReplace = null },
            title = { Text("Replace “${Song.title.ifEmpty { "Untitled" }}”?") },
            text = {
                Text(
                    "The app keeps one song at a time. Export this one first if you want to keep a copy.",
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    confirmReplace = null
                    when (what) {
                        Replace.NEW -> message = Song.loadBundled(context, Song.NEW_SONG)?.let { "Couldn't start a new song: $it" }
                        Replace.DEMO -> message = Song.loadBundled(context)?.let { "Couldn't load the demo song: $it" }
                        // Song files are JSON, but file managers label .json files inconsistently.
                        Replace.FILE -> openFile.launch(arrayOf("application/json", "text/*", "application/octet-stream"))
                    }
                }) { Text(if (what == Replace.FILE) "Choose a file" else "Replace") }
            },
            dismissButton = { TextButton(onClick = { confirmReplace = null }) { Text("Cancel") } },
        )
    }
}

private enum class Replace { NEW, FILE, DEMO }

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun PerformScreen(
    audioOk: Boolean,
    stats: IntArray,
    params: SnapshotStateList<Float>,
    layerOn: SnapshotStateList<Boolean>,
    modifier: Modifier = Modifier,
) {
    val currentSection = stats.getOrElse(5) { -1 }
    val playing = stats.getOrElse(6) { 0 } == 1

    var quantize by rememberSaveable { mutableIntStateOf(NativeAudio.Quantize.BAR.ordinal) }
    var entry by rememberSaveable { mutableIntStateOf(NativeAudio.Entry.RESTART.ordinal) }
    var viaBridge by rememberSaveable { mutableStateOf(false) }
    // The key buttons start at the song's own key. A scale that isn't one of
    // the named ones (a song can list its own notes) shows no scale selected,
    // and changing the root keeps it.
    val home = remember { NativeAudio.getHomeKey() }
    var root by rememberSaveable { mutableIntStateOf(home[0]) }
    var scaleMask by rememberSaveable { mutableIntStateOf(home[1]) }

    run {
        Column(
            modifier = modifier
                .fillMaxSize()
                .padding(horizontal = 16.dp, vertical = 8.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            if (!audioOk) {
                Text("Could not open audio output.", color = MaterialTheme.colorScheme.error)
            }

            // Transport
            Row(verticalAlignment = Alignment.CenterVertically) {
                Button(onClick = { if (playing) NativeAudio.stop() else NativeAudio.play() }) {
                    Text(if (playing) "Stop" else "Play")
                }
                Text(
                    when {
                        !playing -> "  Stopped"
                        currentSection < 0 -> "  Playing a solo pattern"
                        else -> "  Now playing: ${Song.sections.getOrElse(currentSection) { "?" }}"
                    },
                    style = MaterialTheme.typography.bodyLarge,
                )
            }

            // Parameters
            SectionTitle("Parameters")
            Song.params.forEachIndexed { i, name ->
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
                Song.events.forEachIndexed { index, label ->
                    FilledTonalButton(onClick = { NativeAudio.fireEvent(index) }, enabled = playing) {
                        Text(label)
                    }
                }
            }

            // Sections
            SectionTitle("Sections")
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Song.playableSections.forEach { i ->
                    val name = Song.sections[i]
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
            if (Song.hasBridge) Row(verticalAlignment = Alignment.CenterVertically) {
                Switch(checked = viaBridge, onCheckedChange = { viaBridge = it })
                Text("  Play the bridge first")
            }

            // Layers: mute and unmute; in edit mode, make, rename and remove them.
            var editingLayers by rememberSaveable { mutableStateOf(false) }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(Modifier.weight(1f)) { SectionTitle("Layers (mute at the next bar)") }
                TextButton(onClick = { editingLayers = !editingLayers }) { Text(if (editingLayers) "Done" else "Edit") }
            }
            if (editingLayers) {
                LayerEditor(layerOn)
            } else {
                if (Song.layers.isEmpty()) Text("No layers. Tap Edit to make one.", style = MaterialTheme.typography.bodySmall)
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Song.layers.forEachIndexed { i, name ->
                        FilterChip(
                            selected = layerOn.getOrElse(i) { true },
                            onClick = {
                                layerOn[i] = !layerOn[i]
                                NativeAudio.setLayerMute(i, !layerOn[i])
                            },
                            label = { Text(name) },
                        )
                    }
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
                            NativeAudio.setKey(root, scaleMask)
                        },
                        label = { Text(name) },
                    )
                }
            }
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                NativeAudio.Scale.entries.forEach { s ->
                    FilterChip(
                        selected = scaleMask == s.mask,
                        onClick = {
                            scaleMask = s.mask
                            NativeAudio.setKey(root, scaleMask)
                        },
                        label = { Text(s.label) },
                    )
                }
            }

            DiagnosticsCard(stats)
        }
    }
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
            Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
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

/** Shown after a crash: share the report (to paste into a chat) or dismiss it. */
@Composable
private fun CrashCard(report: String, onShare: () -> Unit, onDismiss: () -> Unit) {
    Card(Modifier.fillMaxWidth().padding(8.dp)) {
        Column(Modifier.padding(12.dp)) {
            Text("The app stopped unexpectedly last time.", style = MaterialTheme.typography.titleSmall)
            Text(
                report.lineSequence().drop(3).firstOrNull { it.isNotBlank() } ?: "",
                style = MaterialTheme.typography.bodySmall,
                maxLines = 2,
            )
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                TextButton(onClick = onShare) { Text("Share report") }
                TextButton(onClick = onDismiss) { Text("Dismiss") }
            }
        }
    }
}
