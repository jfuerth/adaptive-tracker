package io.github.jfuerth.adaptivetracker

import android.content.Context
import android.net.Uri
import android.util.AtomicFile
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import io.github.jfuerth.adaptivetracker.editor.PatternStore
import io.github.jfuerth.adaptivetracker.instrument.InstrumentStore
import java.io.File
import java.io.IOException

/**
 * The song being edited: its names and starting values (read from the engine
 * after a song file loads), and where it is kept.
 *
 * The engine holds the song and every edit made to it. The app keeps it in
 * private storage, saving a moment after each change and when the app goes to
 * the background, so it comes back as it was left. Export and import
 * copy it to and from files other apps can see.
 */
object Song {
    /** The song bundled with the app, used the first time and on "Start from the demo song". */
    const val BUNDLED = "adaptive-demo.json"
    /** What "New song" starts from: one channel, one empty pattern, a few instruments. */
    const val NEW_SONG = "new-song.json"
    private const val AUTOSAVE = "song.json"
    /** Larger files aren't songs (the limits make songs a few hundred KB at most). */
    private const val MAX_FILE_BYTES = 8 shl 20

    /** Goes up each time a song loads, so the UI can start afresh. */
    var generation by mutableIntStateOf(0)
        private set
    var title by mutableStateOf("")
        private set
    var sections: List<String> by mutableStateOf(emptyList())
        private set
    /** Per section: beats it plays when used as a bridge (0: not a bridge). */
    var bridgeBeats: List<Int> by mutableStateOf(emptyList())
        private set
    var channels: List<String> by mutableStateOf(emptyList())
        private set

    /** The pattern being edited, shared so other screens can open one in the editor. */
    var pattern by mutableIntStateOf(0)
    var layers: List<String> by mutableStateOf(emptyList())
        private set
    /** Each layer's columns: bit (channel × 4 + column). */
    var layerMasks: List<Int> by mutableStateOf(emptyList())
        private set

    /** [channel]'s columns in [layer], as bits (bit c: column c). */
    fun layerColumns(layer: Int, channel: Int): Int = (layerMasks.getOrElse(layer) { 0 } ushr (channel * 4)) and 0xF
    var params: List<String> = emptyList()
        private set
    var paramInitial: List<Float> = emptyList()
        private set
    var events: List<String> = emptyList()
        private set

    /** Sections to offer as destinations: everything that isn't only a bridge. */
    val playableSections: List<Int> get() = sections.indices.filter { bridgeBeats.getOrElse(it) { 0 } == 0 }
    val hasBridge: Boolean get() = bridgeBeats.any { it > 0 }

    val isLoaded: Boolean get() = generation > 0

    /** Edit count last written to private storage; null: the storage copy is out of date. */
    private var savedEdits: Long? = null

    /**
     * Loads the song the app was left with, or the bundled one the first time.
     * Returns null, or a message for the user if something went wrong.
     */
    fun start(context: Context): String? {
        val file = File(context.filesDir, AUTOSAVE)
        if (file.exists()) {
            val error = try {
                load(AtomicFile(file).readFully())
            } catch (e: IOException) {
                "Could not read it: ${e.message}"
            }
            if (error == null) {
                savedEdits = 0
                return null
            }
            // Keep it for a newer version of the app (or for a bug report) rather than overwrite it.
            val kept = File(context.filesDir, "song-unreadable.json")
            file.renameTo(kept)
            return "Your song couldn't be opened ($error), so the demo song is loaded instead. " +
                "The old file is kept as ${kept.name}."
        }
        return loadBundled(context)
    }

    /** Replaces the song with a bundled one ([BUNDLED] or [NEW_SONG]). Returns null, or why it failed. */
    fun loadBundled(context: Context, name: String = BUNDLED): String? {
        val bytes = try {
            context.assets.open(name).use { it.readBytes() }
        } catch (e: IOException) {
            return "Could not read $name: ${e.message}"
        }
        load(bytes)?.let { return it }
        autosave(context)
        return null
    }

    /** Replaces the song with a file another app provides. Returns null, or why it failed (the song stays). */
    fun import(context: Context, uri: Uri): String? {
        val bytes = try {
            context.contentResolver.openInputStream(uri)?.use { input ->
                val out = java.io.ByteArrayOutputStream()
                val buffer = ByteArray(64 * 1024)
                while (true) {
                    val n = input.read(buffer)
                    if (n < 0) break
                    out.write(buffer, 0, n)
                    if (out.size() > MAX_FILE_BYTES) return "That file is too big to be a song."
                }
                out.toByteArray()
            } ?: return "Could not open that file."
        } catch (e: IOException) {
            return "Could not read that file: ${e.message}"
        } catch (e: SecurityException) {
            return "Not allowed to read that file."
        }
        load(bytes)?.let { return it }
        autosave(context)
        return null
    }

    /** Writes the song to a file another app provides. Returns null, or why it failed. */
    fun export(context: Context, uri: Uri): String? = try {
        val bytes = NativeAudio.saveSong()
        // "wt": replace what's there; plain "w" can leave old bytes past the end.
        val out = context.contentResolver.openOutputStream(uri, "wt")
        out?.use { it.write(bytes) }
        if (out == null) "Could not open that file for writing." else null
    } catch (e: IOException) {
        "Could not write the song: ${e.message}"
    } catch (e: SecurityException) {
        "Not allowed to write that file."
    }

    /** A file name for exporting: the title, made safe for file systems. */
    val exportName: String get() =
        title.replace(Regex("""[\\/:*?"<>|\p{Cntrl}]"""), "").trim().ifEmpty { "song" } + ".json"

    fun rename(newTitle: String) {
        val t = newTitle.trim()
        if (t.isEmpty() || t == title) return
        NativeAudio.setSongTitle(t)
        title = t
    }

    /**
     * Writes the song to private storage if it changed since the last write.
     * UI thread only (it reads the engine's copy of the song).
     */
    fun autosave(context: Context) {
        if (!isLoaded) return
        val edits = NativeAudio.getEditCount()
        if (savedEdits == edits) return
        val bytes = NativeAudio.saveSong()
        val file = AtomicFile(File(context.filesDir, AUTOSAVE))
        val out = try {
            file.startWrite()
        } catch (e: IOException) {
            return
        }
        try {
            out.write(bytes)
            file.finishWrite(out)
            savedEdits = edits
        } catch (e: IOException) {
            file.failWrite(out)
        }
    }

    /** Re-reads section and channel names and timing, after one is added or renamed. */
    fun structureChanged() {
        sections = NativeAudio.getSongNames(NativeAudio.NAMES_SECTIONS).toList()
        bridgeBeats = NativeAudio.getSectionBridgeBeats().toList()
        channels = NativeAudio.getSongNames(NativeAudio.NAMES_CHANNELS).toList()
        layers = NativeAudio.getSongNames(NativeAudio.NAMES_LAYERS).toList()
        layerMasks = NativeAudio.getLayerMasks().toList()
    }

    /** Loads a song file's bytes. Returns null, or why it failed (the old song stays). */
    private fun load(json: ByteArray): String? {
        NativeAudio.loadSong(json)?.let { return it }
        savedEdits = null
        title = NativeAudio.getSongTitle()
        structureChanged()
        pattern = 0
        params = NativeAudio.getSongNames(2).toList()
        paramInitial = NativeAudio.getParamInitialValues().toList()
        events = NativeAudio.getSongNames(3).toList()
        PatternStore.reset()
        InstrumentStore.reset()
        generation++
        return null
    }
}
