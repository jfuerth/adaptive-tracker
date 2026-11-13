#pragma once

#include <adaptive/engine.hpp>
#include <demo/adaptive_demo.hpp>

#include <oboe/Oboe.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

/**
 * Plays the engine through an Oboe output stream.
 *
 * Threading: control methods (play, gotoSection, setParam, ...) post commands
 * to the engine's lock-free queue and must all be called from one thread (the
 * UI thread). The engine itself is only touched on the audio thread, which
 * never waits on a lock. open()/close() are called from the UI thread.
 */
class AudioPlayer : public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback {
public:
    static constexpr int32_t kSampleRate = 48000;

    AudioPlayer();
    ~AudioPlayer() override;

    /// Open and start the output stream. Returns false on failure.
    bool open();
    void close();

    // Control (UI thread only). Each returns false if the command queue was full.
    bool play();
    bool stop();
    bool gotoSection(int section, int quantize, int entry, bool viaBridge);
    bool fireEvent(int event);
    bool setLayerMute(int layer, bool muted);
    bool setParam(int param, float value);
    /// root: pitch class 0-11. scaleIndex: index into kScales (see .cpp). Applies at the next bar.
    bool setKey(int root, int scaleIndex);

    // Pattern editing (UI thread only).
    uint32_t patternCount() const { return adaptive::demo::AdaptiveDemo::kNumPatterns; }
    const char* patternName(uint32_t i) { return song_->patternEntry(i).name; }
    /// [channel, numRows, rowsPerBeat, numColumns, pitchMode (0 Absolute, 1 Transpose, 2 Degree)]
    bool patternInfo(uint32_t i, int32_t out[5]);
    /// Copies the pattern's cells, 5 bytes each (note, accidental, volume, effect,
    /// param), row by row, numColumns per row. Only call before editing that
    /// pattern; afterwards the UI keeps its own copy.
    bool readPattern(uint32_t i, uint8_t* out, uint32_t capacity);
    bool setCell(uint32_t pattern, int row, int column, const adaptive::Cell& cell);
    bool previewOn(int channel, int slot, int midiNote);
    bool previewOff(int slot);
    /// Loop pattern-table entry `pattern` alone; `columnMask` picks the columns heard.
    bool soloPattern(uint32_t pattern, int columnMask);
    bool setColumnMask(int channel, int columnMask);

    /// Song's home key: [root, scale mask].
    void homeKey(int32_t out[2]) const;

    /// Playback health and state, for the UI. Safe to call from any thread.
    struct Stats {
        int32_t underruns;         ///< Since the stream opened.
        int32_t bufferFrames;      ///< Current buffer size (the tuner raises it after underruns).
        int32_t burstFrames;
        int32_t maxCallbackUs;     ///< Slowest callback since the last resetStats().
        int32_t callbackBudgetUs;  ///< Audio duration of the slowest callback's buffer.
        int32_t currentSection;    ///< Section playing now, or -1.
        int32_t playing;           ///< 1 if anything plays (a section or a solo pattern).
    };
    Stats stats() const;

    /// Pattern-table index playing on `channel` (-1 if none) and its row. Any thread.
    void playPosition(int channel, int32_t out[2]) const;
    /// The key in effect now: [root, scale mask]. Any thread.
    void currentKey(int32_t out[2]) const;
    void resetStats() { resetStatsRequested_.store(true); }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream, void* audioData,
                                          int32_t numFrames) override;
    void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override;

private:
    bool openLocked();  // streamMutex_ must be held
    void updateStats(oboe::AudioStream* stream, int64_t callbackNs, int32_t numFrames);

    std::unique_ptr<adaptive::Engine> engine_;
    std::unique_ptr<adaptive::demo::AdaptiveDemo> song_;
    std::shared_ptr<oboe::AudioStream> stream_;
    std::mutex streamMutex_;  // guards stream_ between UI thread and error-restart thread
    std::atomic<bool> wantOpen_{false};
    std::unique_ptr<oboe::LatencyTuner> latencyTuner_;  // created with the stream

    // Written by the audio thread.
    std::atomic<int32_t> underruns_{0};
    std::atomic<int32_t> bufferFrames_{0};
    std::atomic<int32_t> burstFrames_{0};
    std::atomic<int32_t> maxCallbackUs_{0};
    std::atomic<int32_t> callbackBudgetUs_{0};
    std::atomic<int32_t> currentSection_{-1};
    std::atomic<int32_t> playing_{0};
    std::atomic<int32_t> playingPattern_[adaptive::config::kMaxChannels];
    std::atomic<int32_t> playingRow_[adaptive::config::kMaxChannels];
    std::atomic<int32_t> keyRoot_{0};
    std::atomic<int32_t> keyScale_{adaptive::scales::kMajor};
    std::atomic<bool> resetStatsRequested_{false};
};
