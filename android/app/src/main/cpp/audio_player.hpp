#pragma once

#include <adaptive/engine.hpp>
#include <demo/degree_demo.hpp>

#include <oboe/Oboe.h>

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

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream, void* audioData,
                                          int32_t numFrames) override;
    void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override;

private:
    bool openLocked();  // streamMutex_ must be held
    void applyRequests();

    std::unique_ptr<adaptive::Engine> engine_;
    std::unique_ptr<adaptive::demo::DegreeDemo> song_;
    std::shared_ptr<oboe::AudioStream> stream_;
    std::mutex streamMutex_;  // guards stream_ between UI thread and error-restart thread
    std::atomic<bool> wantOpen_{false};

    std::atomic<bool> requestedPlaying_{false};
    std::atomic<int> requestedRoot_{0};
    std::atomic<int> requestedScale_{0};
    std::atomic<uint32_t> requestedTempo_{adaptive::demo::DegreeDemo::kDefaultTempoMilliBpm};

    // Audio-thread state.
    bool playing_ = false;
    int root_ = -1;
    int scale_ = -1;
    uint32_t tempo_ = 0;
};
