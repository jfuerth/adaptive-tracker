#pragma once

#include <adaptive/engine.hpp>
#include <demo/degree_demo.hpp>

#include <oboe/Oboe.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

/**
 * Plays the engine through an Oboe output stream.
 *
 * Threading: UI-thread setters only write atomics. The audio callback reads
 * them at the start of each buffer and applies changes to the engine, so the
 * engine is only ever touched on the audio thread and the callback never
 * waits on a lock. open()/close() are called from the UI thread.
 */
class AudioPlayer : public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback {
public:
    static constexpr int32_t kSampleRate = 48000;

    AudioPlayer();
    ~AudioPlayer() override;

    /// Open and start the output stream. Returns false on failure.
    bool open();
    void close();

    void setPlaying(bool playing) { requestedPlaying_.store(playing); }
    /// root: pitch class 0-11. scaleIndex: index into kScales (see .cpp).
    void setKey(int root, int scaleIndex) {
        requestedRoot_.store(root);
        requestedScale_.store(scaleIndex);
    }
    void setTempoBpm(float bpm) { requestedTempo_.store(static_cast<uint32_t>(bpm * 1000.0f)); }

    /// Playback health, for the diagnostics panel. Safe to call from any thread.
    struct Stats {
        int32_t underruns;        ///< Since the stream opened.
        int32_t bufferFrames;     ///< Current buffer size (the tuner raises it after underruns).
        int32_t burstFrames;
        int32_t maxCallbackUs;    ///< Slowest callback since the last resetStats().
        int32_t callbackBudgetUs; ///< Audio duration of the slowest callback's buffer.
        int32_t lastUnderrunRow;  ///< Melody row (0-15) when the last underrun was seen, or -1.
    };
    Stats stats() const;
    void resetStats() { resetStatsRequested_.store(true); }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream, void* audioData,
                                          int32_t numFrames) override;
    void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override;

private:
    bool openLocked();  // streamMutex_ must be held
    void applyRequests();
    void updateStats(oboe::AudioStream* stream, int64_t callbackNs, int32_t numFrames);

    std::unique_ptr<adaptive::Engine> engine_;
    std::unique_ptr<adaptive::demo::DegreeDemo> song_;
    std::shared_ptr<oboe::AudioStream> stream_;
    std::mutex streamMutex_;  // guards stream_ between UI thread and error-restart thread
    std::atomic<bool> wantOpen_{false};
    std::unique_ptr<oboe::LatencyTuner> latencyTuner_;  // created with the stream

    // Diagnostics, written by the audio thread.
    std::atomic<int32_t> underruns_{0};
    std::atomic<int32_t> bufferFrames_{0};
    std::atomic<int32_t> burstFrames_{0};
    std::atomic<int32_t> maxCallbackUs_{0};
    std::atomic<int32_t> callbackBudgetUs_{0};
    std::atomic<int32_t> lastUnderrunRow_{-1};
    std::atomic<bool> resetStatsRequested_{false};

    std::atomic<bool> requestedPlaying_{false};
    std::atomic<int> requestedRoot_{0};
    std::atomic<int> requestedScale_{0};
    std::atomic<uint32_t> requestedTempo_{adaptive::demo::DegreeDemo::kDefaultTempoMilliBpm};

    // Audio-thread state.
    bool playing_ = false;
    int root_ = -1;
    int scale_ = -1;
    uint32_t tempo_ = 0;
    uint64_t songStartTick_ = 0;
};
