package io.github.jfuerth.adaptivetracker

/**
 * Bridge to the C++ engine (android/app/src/main/cpp), playing a song loaded
 * from a song file (see Song).
 *
 * Control calls post commands to the engine's lock-free queue: they are cheap,
 * never block, and must all be made from the main (UI) thread. Each returns
 * false if the queue was full and the command was dropped.
 */
object NativeAudio {
    init {
        System.loadLibrary("adaptive_native")
    }

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
        MINOR_PENTATONIC("Minor pentatonic");

        val mask: Int get() = namedScaleMasks.getOrElse(ordinal) { 0xFFF }
    }

    val NOTE_NAMES = listOf("C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B")

    /** Opens and starts the audio output stream. Returns false on failure. */
    external fun open(): Boolean

    external fun close()

    /**
     * Replaces the song with a song file's text; playback stops. Returns null,
     * or why the file couldn't be loaded (the old song stays).
     */
    external fun loadSong(json: ByteArray): String?

    /** The song as a song file (UTF-8), with every edit made so far. */
    external fun saveSong(): ByteArray

    /** Goes up with every edit; unchanged means there's nothing new to save. */
    external fun getEditCount(): Long

    external fun getSongTitle(): String
    private external fun setSongTitle(title: ByteArray)
    fun setSongTitle(title: String) = setSongTitle(title.encodeToByteArray())

    /** Names in the song: [kind] 0 sections, 1 layers, 2 parameters, 3 events. */
    external fun getSongNames(kind: Int): Array<String>

    /** Per section, its bridge length in beats (0: not a bridge). */
    external fun getSectionBridgeBeats(): IntArray

    external fun getParamInitialValues(): FloatArray

    /** Effect ticks per beat; a pattern's speed (ticks per row) counts these. */
    external fun getTicksPerBeat(): Int

    /**
     * Starts the song at its start section. [hearAll]: every note plays, ignoring
     * column gates, layer mutes and layer gains (the editor's plain Play);
     * otherwise the adaptive mix.
     */
    external fun play(hearAll: Boolean = false): Boolean

    /** Switches what is playing between every note ([on]) and the adaptive mix. */
    external fun setHearAll(on: Boolean): Boolean

    /** Stops all channels; sounding notes release. */
    external fun stop(): Boolean

    external fun gotoSection(section: Int, quantize: Int, entry: Int, viaBridge: Boolean): Boolean

    external fun fireEvent(event: Int): Boolean

    /** Musical mute, at the next bar. */
    external fun setLayerMute(layer: Int, muted: Boolean): Boolean

    /** [value] is 0..1. */
    external fun setParam(param: Int, value: Float): Boolean

    /** [root] is a pitch class 0-11 (0 = C); [scale] a 12-bit mask. Applies at the next bar. */
    external fun setKey(root: Int, scale: Int): Boolean

    /** The masks of the [Scale]s, in their order. */
    external fun getScaleMasks(): IntArray

    /** Each named scale's mask. */
    val namedScaleMasks: IntArray by lazy { getScaleMasks() }

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

    /** [channel, numRows, ticksPerRow, numColumns, pitchMode], or null. */
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
    /** Pattern [pattern] gets [rows] rows (1-256) at [ticksPerRow] (1-255). Rows past the end keep their cells. */
    external fun setPatternShape(pattern: Int, rows: Int, ticksPerRow: Int): Boolean

    /**
     * Empties rows [first] to [last] (inclusive) of [column] in pattern
     * [pattern]: the parts in [parts] (CELL_NOTE, CELL_VOLUME, CELL_EFFECT bits).
     */
    external fun clearCells(pattern: Int, column: Int, first: Int, last: Int, parts: Int): Boolean

    /**
     * Stretches or squashes pattern [pattern]: row r moves to r × [num] / [den]
     * (rows that don't land on a whole row are dropped), and the length scales
     * to match, rounding up. False if it would be over 256 rows.
     */
    external fun scaleRows(pattern: Int, num: Int, den: Int): Boolean

    // ---- Growing the song: each returns the new index, or -1 when the song is full ----

    /** A pattern for [channel], empty or a copy of pattern [copyOf] (-1: empty). */
    fun addPattern(channel: Int, name: String, copyOf: Int = -1): Int = addPatternBytes(channel, name.toByteArray(), copyOf)
    /** A channel with one column playing [instrument] (-1: none); [mode]: PitchMode ordinal. */
    fun addChannel(name: String, mode: Int, instrument: Int): Int = addChannelBytes(name.toByteArray(), mode, instrument)
    fun addSection(name: String): Int = addSectionBytes(name.toByteArray())
    /** Renames item [index] of [kind] (getSongNames' kinds: NAMES_*). Its id stays. */
    fun rename(kind: Int, index: Int, name: String): Boolean = renameBytes(kind, index, name.toByteArray())
    private external fun addPatternBytes(channel: Int, name: ByteArray, copyOf: Int): Int
    private external fun addChannelBytes(name: ByteArray, mode: Int, instrument: Int): Int
    private external fun addSectionBytes(name: ByteArray): Int
    private external fun renameBytes(kind: Int, index: Int, name: ByteArray): Boolean

    /** Changes what [channel] plays in [section]; [op]: SEQ_INSERT, SEQ_REMOVE or SEQ_SET at entry [pos]. */
    external fun editSequence(section: Int, channel: Int, op: Int, pos: Int, pattern: Int): Boolean
    /** Section timing: [bpm] 0 keeps the tempo; [bridgeBeats] 0: not a bridge. */
    external fun setSection(section: Int, bpm: Float, beatsPerBar: Int, bridgeBeats: Int): Boolean
    /** Pattern indexes [channel] plays in [section], in order. */
    external fun getSequence(section: Int, channel: Int): IntArray?
    /** [tempo in milli-BPM (0: keeps the tempo), beats per bar, bridge beats]. */
    external fun getSectionTiming(section: Int): IntArray?

    // ---- Column settings and layers ----

    /** Per column of [channel]: [pitch mode, gate parameter (-1: none), gate threshold × 1000], flattened. */
    external fun getColumnSettings(channel: Int): IntArray
    /** The column reads notes in [mode] (PitchMode ordinal); its notes are rewritten to sound the same. */
    external fun setColumnPitch(channel: Int, column: Int, mode: Int): Boolean
    /** The column starts notes only while [param] is at least [threshold] (param -1: always). */
    external fun setColumnGate(channel: Int, column: Int, param: Int, threshold: Float): Boolean
    fun addLayer(name: String): Int = addLayerBytes(name.toByteArray())
    private external fun addLayerBytes(name: ByteArray): Int
    /** Takes out the layer, with mappings and event actions that refer to it. */
    external fun removeLayer(layer: Int): Boolean

    // Taking things out; later ones move down. Playback carries on in place.
    /** Takes out the pattern, its places in sequences and the stingers that play it. */
    external fun deletePattern(pattern: Int): Boolean
    /** Takes out the channel with its patterns, its place in layers, and mappings and stingers aimed at it. */
    external fun deleteChannel(channel: Int): Boolean
    /** Takes out the section, and event actions that go to it. */
    external fun deleteSection(section: Int): Boolean

    // Undo: each edit is a step, or several made between beginStep and endStep.
    external fun undo(): Boolean
    external fun redo(): Boolean
    /** What undo would change, or "" if nothing. */
    external fun getUndoLabel(): String
    external fun getRedoLabel(): String
    fun beginStep(label: String) = beginStepBytes(label.toByteArray())
    private external fun beginStepBytes(label: ByteArray)
    external fun endStep()
    /** The layer holds [channel]'s columns in [columnBits] (bit c: column c). */
    external fun setLayerColumns(layer: Int, channel: Int, columnBits: Int): Boolean
    /** Each layer's columns, bit (channel × 4 + column). */
    external fun getLayerMasks(): IntArray
    /** An instrument's pitch-mode hint, -1 if none. */
    external fun getInstrumentPitch(instrument: Int): Int
    external fun setInstrumentPitch(instrument: Int, mode: Int): Boolean

    const val NAMES_SECTIONS = 0
    const val NAMES_LAYERS = 1
    const val NAMES_PARAMS = 2
    const val NAMES_EVENTS = 3
    const val NAMES_CHANNELS = 4
    const val NAMES_PATTERNS = 5
    const val SEQ_INSERT = 0
    const val SEQ_REMOVE = 1
    const val SEQ_SET = 2

    const val CELL_NOTE = 1
    const val CELL_VOLUME = 2
    const val CELL_EFFECT = 4
    const val CELL_ALL = 7

    /** [channel] gets [count] columns (1-4), in all its patterns; a removed column comes back as it was. */
    external fun setColumns(channel: Int, count: Int): Boolean

    /** Removes [channel]'s [column] from all its patterns; later columns move left. */
    external fun removeColumn(channel: Int, column: Int): Boolean

    external fun previewOn(instrument: Int, slot: Int, midiNote: Int): Boolean

    external fun getChannelCount(): Int
    external fun getChannelName(index: Int): String
    /** Number of columns (voices) on channel [index]. */
    external fun getChannelColumns(index: Int): Int
    external fun getInstrumentCount(): Int
    external fun getInstrumentName(index: Int): String
    private external fun renameInstrument(index: Int, name: ByteArray): Boolean
    /** Renames an instrument; the song file keeps referring to it by its id. */
    fun renameInstrument(index: Int, name: String) = renameInstrument(index, name.encodeToByteArray())

    /**
     * Instrument [index]'s parameter values, in InstrumentParam order. Read each
     * instrument once, before editing it; InstrumentStore keeps the UI's copy.
     */
    external fun readInstrument(index: Int): FloatArray?

    /** How many instruments a song can have. */
    external fun getMaxInstruments(): Int

    /**
     * Adds an instrument called [name] with [values] (engine parameter order),
     * or the synth's defaults when null. Returns its index, or -1 if the song is full.
     */
    private external fun addInstrument(values: FloatArray?, name: ByteArray): Int
    fun addInstrument(values: FloatArray?, name: String) = addInstrument(values, name.encodeToByteArray())

    /** [channel]'s [column] plays instrument [instrument] from now on. */
    external fun assignInstrument(channel: Int, column: Int, instrument: Int): Boolean

    /** Which instrument [readMonitor] hears (-1: none). */
    external fun setMonitor(instrument: Int): Boolean

    /**
     * The latest [n] samples (up to 8192, oldest first) of the monitored
     * instrument's voices alone, before channel effects and the master stage.
     */
    external fun readMonitor(n: Int): FloatArray?

    /** Sets one parameter; applied on the audio thread to every voice using the instrument. */
    external fun setInstrumentParam(index: Int, param: Int, value: Float): Boolean

    /** Each instrument parameter's stable key, in the engine's order. */
    external fun getInstrumentParamKeys(): Array<String>

    /** [min, max] per instrument parameter, in the engine's order. */
    external fun getInstrumentParamRanges(): FloatArray

    /** [n] samples of the synth's own oscillator at [shape], spanning [cycles] cycles. */
    external fun renderWaveform(shape: Float, cycles: Float, n: Int): FloatArray?

    /**
     * The synth's own envelope for a note held until sustain, then released:
     * [attack s, decay s, release s, releaseUnfinished (0/1), then 32 levels
     * each for attack, decay and release].
     */
    external fun traceEnvelope(attack: Float, decay: Float, sustain: Float, release: Float): FloatArray

    /** An effect parameter in natural units (Hz for F, Q for 5, 0..1 for 6), or NaN. */
    external fun effectValue(effect: Int, param: Int): Float

    /** Instrument index played by [channel]'s [column], or -1. */
    external fun getColumnInstrument(channel: Int, column: Int): Int

    external fun previewOff(slot: Int): Boolean

    /** Stops everything and loops pattern [pattern] alone; [columnMask] picks the columns heard; see [play] for [hearAll]. */
    external fun soloPattern(pattern: Int, columnMask: Int, hearAll: Boolean): Boolean

    /** Channel [channel] plays only the columns in [columnMask] (bit 0 = column 0). */
    external fun setColumnMask(channel: Int, columnMask: Int): Boolean

    /** [patternIndex or -1, row or -1] playing on [channel]. */
    external fun getPlayPosition(channel: Int): IntArray

    /** [root, scaleMask] of the key in effect now. */
    external fun getCurrentKey(): IntArray

    /** [root, scaleMask] of the song's home key. */
    external fun getHomeKey(): IntArray
}
