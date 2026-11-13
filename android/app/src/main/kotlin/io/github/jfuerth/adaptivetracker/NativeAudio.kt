package io.github.jfuerth.adaptivetracker

/**
 * Bridge to the C++ engine (android/app/src/main/cpp), playing the adaptive demo
 * song (core/demo/adaptive_demo.hpp).
 *
 * Control calls post commands to the engine's lock-free queue: they are cheap,
 * never block, and must all be made from the main (UI) thread. Each returns
 * false if the queue was full and the command was dropped.
 *
 * The index constants mirror AdaptiveDemo in adaptive_demo.hpp.
 */
object NativeAudio {
    init {
        System.loadLibrary("adaptive_native")
    }

    val SECTIONS = listOf("Explore", "Combat")  // the bridge (index 2) is only used in transitions
    const val SECTION_BRIDGE = 2
    val SECTION_NAMES = listOf("Explore", "Combat", "Bridge")

    val LAYERS = listOf("Rhythm", "Melody", "Pad")
    val PARAMS = listOf("Intensity", "Danger")

    /** All of the demo's events, by index (used to describe Bxx effects). */
    val EVENT_NAMES = listOf("Hit", "Pickup", "Enter combat", "Calm down", "Alarm")

    /** Events with buttons: index into the demo's events, and a label. */
    val EVENT_BUTTONS = listOf(0 to "Hit", 1 to "Pickup", 4 to "Alarm")

    enum class Quantize(val label: String) { NOW("Now"), BEAT("Beat"), BAR("Bar") }
    enum class Entry(val label: String) { RESTART("Restart"), KEEP_PHASE("Keep phase") }

    /** Scales offered in the UI. Order must match kScales in audio_player.cpp. */
    enum class Scale(val label: String) {
        MAJOR("Major"),
        NATURAL_MINOR("Minor"),
        HARMONIC_MINOR("Harmonic minor"),
        DORIAN("Dorian"),
        PHRYGIAN("Phrygian"),
        LYDIAN("Lydian"),
        MIXOLYDIAN("Mixolydian"),
        LOCRIAN("Locrian"),
        MAJOR_PENTATONIC("Major pentatonic"),
        MINOR_PENTATONIC("Minor pentatonic"),
    }

    val NOTE_NAMES = listOf("C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B")

    /** Opens and starts the audio output stream. Returns false on failure. */
    external fun open(): Boolean

    external fun close()

    /** Starts the song at the Explore section. */
    external fun play(): Boolean

    /** Stops all channels; sounding notes release. */
    external fun stop(): Boolean

    external fun gotoSection(section: Int, quantize: Int, entry: Int, viaBridge: Boolean): Boolean

    external fun fireEvent(event: Int): Boolean

    /** Musical mute, at the next bar. */
    external fun setLayerMute(layer: Int, muted: Boolean): Boolean

    /** [value] is 0..1. */
    external fun setParam(param: Int, value: Float): Boolean

    /** [root] is a pitch class 0-11 (0 = C). Applies at the next bar. */
    external fun setKey(root: Int, scaleIndex: Int): Boolean

    /**
     * [underruns, bufferFrames, burstFrames, maxCallbackUs, callbackBudgetUs,
     * currentSection, playing]; currentSection is -1 when no section plays
     * (stopped, or soloing a pattern); playing is 1 while anything plays.
     */
    external fun getStats(): IntArray

    /** Clears the slowest-callback measurement. */
    external fun resetStats()

    // ---- Pattern editing ----

    external fun getPatternCount(): Int

    external fun getPatternName(index: Int): String

    /** [channel, numRows, rowsPerBeat, numColumns, pitchMode], or null. */
    external fun getPatternInfo(index: Int): IntArray?

    /**
     * The pattern's cells, 5 bytes each (note, accidental, volume, effect, param),
     * row by row. Read each pattern once, before editing it; PatternStore keeps
     * the UI's copy from then on.
     */
    external fun readPattern(index: Int): ByteArray?

    /** Writes one cell; applied on the audio thread, so it is safe while playing. */
    external fun setCell(
        pattern: Int, row: Int, column: Int,
        note: Int, accidental: Int, volume: Int, effect: Int, param: Int,
    ): Boolean

    /** Sounds [midiNote] with song instrument [instrument] on preview voice [slot] (0-3). */
    external fun previewOn(instrument: Int, slot: Int, midiNote: Int): Boolean

    external fun getChannelCount(): Int
    external fun getChannelName(index: Int): String
    external fun getInstrumentCount(): Int
    external fun getInstrumentName(index: Int): String

    /**
     * Instrument [index]'s parameter values, in InstrumentParam order. Read each
     * instrument once, before editing it; InstrumentStore keeps the UI's copy.
     */
    external fun readInstrument(index: Int): FloatArray?

    /** Sets one parameter; applied on the audio thread to every voice using the instrument. */
    external fun setInstrumentParam(index: Int, param: Int, value: Float): Boolean

    /** Instrument index played by [channel]'s [column], or -1. */
    external fun getColumnInstrument(channel: Int, column: Int): Int

    external fun previewOff(slot: Int): Boolean

    /** Stops everything and loops pattern [pattern] alone; [columnMask] picks the columns heard. */
    external fun soloPattern(pattern: Int, columnMask: Int): Boolean

    /** Channel [channel] plays only the columns in [columnMask] (bit 0 = column 0). */
    external fun setColumnMask(channel: Int, columnMask: Int): Boolean

    /** [patternIndex or -1, row or -1] playing on [channel]. */
    external fun getPlayPosition(channel: Int): IntArray

    /** [root, scaleMask] of the key in effect now. */
    external fun getCurrentKey(): IntArray

    /** [root, scaleMask] of the song's home key. */
    external fun getHomeKey(): IntArray
}
