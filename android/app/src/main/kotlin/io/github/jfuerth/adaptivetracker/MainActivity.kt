package io.github.jfuerth.adaptivetracker

import android.content.Intent
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
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
import io.github.jfuerth.adaptivetracker.mixer.MixerScreen
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
    // The pattern editor opens from a channel on the mixer, and goes back there.
    var editing by rememberSaveable { mutableStateOf(false) }
    fun backToMixer() {
        editing = false
        NativeAudio.setHearAll(false)  // the mixer always hears the adaptive mix
    }
    BackHandler(enabled = editing) { backToMixer() }
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
            if (editing) {
                val channel = NativeAudio.getPatternInfo(Song.pattern)?.getOrNull(0) ?: -1
                val channelName = Song.channels.getOrElse(channel) { "Pattern" }
                // Arranging, the cursor can be in any channel: say which, after the section.
                val title = if (Song.arrange) "${Song.sections.getOrElse(Song.section) { "Section" }} · $channelName" else channelName
                SongBar(stats, editingChannel = title, onBack = ::backToMixer)
            } else {
                SongBar(stats)
                PrimaryTabRow(selectedTabIndex = tab) {
                    TABS.forEachIndexed { i, name ->
                        Tab(selected = tab == i, onClick = { tab = i }, text = { Text(name, maxLines = 1) })
                    }
                }
            }
            // A new song starts every screen afresh.
            key(Song.generation) {
                // Held here so they survive switching tabs. Parameters start where the song says.
                val params = remember { Song.paramInitial.toMutableStateList() }
                val layerOn = remember { Song.layers.map { true }.toMutableStateList() }
                // Undo and redo can add or take away layers.
                LaunchedEffect(Song.revision) {
                    while (layerOn.size < Song.layers.size) layerOn.add(true)
                    while (layerOn.size > Song.layers.size) layerOn.removeAt(layerOn.lastIndex)
                }
                // Each tab keeps its state (choices, scroll positions) while another is shown.
                val tabState = rememberSaveableStateHolder()
                val screen = when {
                    editing -> "editor"
                    tab == 0 -> "mixer"
                    else -> "instruments"
                }
                tabState.SaveableStateProvider(screen) {
                    when (screen) {
                        "mixer" -> MixerScreen(
                            audioOk = audioOk,
                            playing = playing,
                            currentSection = stats.getOrElse(5) { -1 },
                            params = params,
                            layerOn = layerOn,
                            onOpenPattern = { p, section ->
                                Song.pattern = p
                                Song.section = section
                                Song.arrange = false
                                editing = true
                            },
                            onArrange = { section ->
                                // The cursor starts in the first pattern the section plays.
                                val first = Song.channels.indices.firstNotNullOfOrNull { ch ->
                                    NativeAudio.getSequence(section, ch)?.firstOrNull()
                                }
                                if (first != null) Song.pattern = first
                                Song.section = section
                                Song.arrange = true
                                editing = true
                            },
                            modifier = Modifier.weight(1f),
                        )
                        "editor" -> EditorScreen(playing = playing, modifier = Modifier.weight(1f))
                        else -> InstrumentScreen(Modifier.weight(1f))
                    }
                }
            }
        }
    }
}

private val TABS = listOf("Mixer", "Instruments")

/**
 * The song's title, undo and redo, and a menu to rename it, open and export
 * songs. In the pattern editor ([editingChannel] set) it starts with a back
 * arrow and the channel's name instead of the title.
 */
@Composable
private fun SongBar(stats: IntArray, editingChannel: String? = null, onBack: () -> Unit = {}) {
    val context = LocalContext.current
    var menuOpen by remember { mutableStateOf(false) }
    var renaming by remember { mutableStateOf(false) }
    /** What a confirmed replace loads. */
    var confirmReplace by remember { mutableStateOf<Replace?>(null) }
    var message by remember { mutableStateOf<String?>(null) }
    var diagnostics by remember { mutableStateOf(false) }

    val openFile = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) message = Song.import(context, uri)?.let { "Couldn't open that song: $it" }
    }
    val exportFile = rememberLauncherForActivityResult(
        ActivityResultContracts.CreateDocument("application/json"),
    ) { uri ->
        if (uri != null) message = Song.export(context, uri) ?: "Exported."
    }

    Row(
        Modifier.fillMaxWidth().padding(start = if (editingChannel != null) 0.dp else 16.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (editingChannel != null) {
            TextButton(onClick = onBack) { Text("←", style = MaterialTheme.typography.titleLarge) }
        }
        Text(
            editingChannel ?: Song.title.ifEmpty { "Untitled" },
            style = MaterialTheme.typography.titleMedium,
            maxLines = 1,
            overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis,
            modifier = Modifier.weight(1f),
        )
        // Undo and redo: what they would change is read a few times a second,
        // since edits come from every screen.
        var undoLabel by remember { mutableStateOf("") }
        var redoLabel by remember { mutableStateOf("") }
        LaunchedEffect(Unit) {
            while (true) {
                undoLabel = NativeAudio.getUndoLabel()
                redoLabel = NativeAudio.getRedoLabel()
                kotlinx.coroutines.delay(250)
            }
        }
        TextButton(enabled = undoLabel.isNotEmpty(), onClick = {
            message = Song.undo()?.let { "Undid: $it" }
            undoLabel = NativeAudio.getUndoLabel()
            redoLabel = NativeAudio.getRedoLabel()
        }) { Text("Undo") }
        TextButton(enabled = redoLabel.isNotEmpty(), onClick = {
            message = Song.redo()?.let { "Redid: $it" }
            undoLabel = NativeAudio.getUndoLabel()
            redoLabel = NativeAudio.getRedoLabel()
        }) { Text("Redo") }
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
                DropdownMenuItem(text = { Text("New song…") }, onClick = {
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
                DropdownMenuItem(text = { Text("Audio diagnostics…") }, onClick = {
                    menuOpen = false
                    diagnostics = true
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

    if (diagnostics) {
        AlertDialog(
            onDismissRequest = { diagnostics = false },
            text = { DiagnosticsCard(stats) },
            confirmButton = { TextButton(onClick = { diagnostics = false }) { Text("Close") } },
        )
    }

    if (renaming) NameDialog("Rename song", Song.title, "Rename") { name ->
        renaming = false
        if (name != null) Song.rename(name)
    }

    // A new song is named as it starts; the same dialog warns that it replaces this one.
    if (confirmReplace == Replace.NEW) NameDialog(
        "New song",
        NEW_SONG_TITLE,
        "Replace",
        message = "Replaces “${Song.title.ifEmpty { "Untitled" }}”: the app keeps one song at a time. " +
            "Export this one first if you want to keep a copy.",
    ) { name ->
        confirmReplace = null
        if (name != null) {
            message = Song.loadBundled(context, Song.NEW_SONG)?.let { "Couldn't start a new song: $it" }
                ?: run { Song.rename(name, undoable = false); null }
        }
    } else confirmReplace?.let { what ->
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
                        Replace.NEW -> {}  // asked above, with a name
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

/** The title new-song.json carries, suggested for a new song. */
private const val NEW_SONG_TITLE = "New song"


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
