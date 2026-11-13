package io.github.jfuerth.adaptivetracker

import android.content.Context

/**
 * The loaded song's names and starting values, read from the engine after
 * a song file loads. Patterns and instruments have their own stores.
 */
object Song {
    /** The song bundled with the app until songs can be saved and opened. */
    const val BUNDLED = "adaptive-demo.json"

    var title = ""
        private set
    var sections: List<String> = emptyList()
        private set
    /** Per section: beats it plays when used as a bridge (0: not a bridge). */
    var bridgeBeats: List<Int> = emptyList()
        private set
    var layers: List<String> = emptyList()
        private set
    var params: List<String> = emptyList()
        private set
    var paramInitial: List<Float> = emptyList()
        private set
    var events: List<String> = emptyList()
        private set

    /** Sections to offer as destinations: everything that isn't only a bridge. */
    val playableSections: List<Int> get() = sections.indices.filter { bridgeBeats.getOrElse(it) { 0 } == 0 }
    val hasBridge: Boolean get() = bridgeBeats.any { it > 0 }

    /** Loads a song file from the app's assets. Returns null, or why it failed. */
    fun loadAsset(context: Context, name: String = BUNDLED): String? {
        val json = try {
            context.assets.open(name).use { it.readBytes().decodeToString() }
        } catch (e: java.io.IOException) {
            return "Could not read $name: ${e.message}"
        }
        return load(json)
    }

    /** Loads a song file's text. Returns null, or why it failed. */
    fun load(json: String): String? {
        NativeAudio.loadSong(json)?.let { return it }
        title = NativeAudio.getSongTitle()
        sections = NativeAudio.getSongNames(0).toList()
        bridgeBeats = NativeAudio.getSectionBridgeBeats().toList()
        layers = NativeAudio.getSongNames(1).toList()
        params = NativeAudio.getSongNames(2).toList()
        paramInitial = NativeAudio.getParamInitialValues().toList()
        events = NativeAudio.getSongNames(3).toList()
        return null
    }
}
