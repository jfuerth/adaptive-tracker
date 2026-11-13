package io.github.jfuerth.adaptivetracker

/**
 * Bridge to the C++ engine (android/app/src/main/cpp). Every setter is cheap and
 * thread-safe: it stores a value that the audio thread picks up on its next buffer.
 */
object NativeAudio {
    init {
        System.loadLibrary("adaptive_native")
    }

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

    external fun setPlaying(playing: Boolean)

    /** [root] is a pitch class 0-11 (0 = C). */
    external fun setKey(root: Int, scaleIndex: Int)

    external fun setTempo(bpm: Float)
}
