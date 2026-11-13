#include "audio_player.hpp"

#include <android/log.h>


#define LOG_TAG "AdaptiveTracker"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {
// Must match the order of Scale entries in NativeAudio.kt.
constexpr uint16_t kScales[] = {
    adaptive::scales::kMajor,          adaptive::scales::kNaturalMinor,
    adaptive::scales::kHarmonicMinor,  adaptive::scales::kDorian,
    adaptive::scales::kPhrygian,       adaptive::scales::kLydian,
    adaptive::scales::kMixolydian,     adaptive::scales::kLocrian,
    adaptive::scales::kMajorPentatonic, adaptive::scales::kMinorPentatonic,
};
constexpr int kNumScales = sizeof(kScales) / sizeof(kScales[0]);
} // namespace

AudioPlayer::AudioPlayer()
    : engine_(std::make_unique<adaptive::Engine>(kSampleRate)),
      song_(std::make_unique<adaptive::demo::DegreeDemo>()) {
    song_->setUpChannels(*engine_);
}

AudioPlayer::~AudioPlayer() { close(); }

bool AudioPlayer::open() {
    wantOpen_.store(true);
    std::lock_guard<std::mutex> lock(streamMutex_);
    return openLocked();
}

bool AudioPlayer::openLocked() {
    if (stream_) return true;

    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setSharingMode(oboe::SharingMode::Exclusive)
        ->setFormat(oboe::AudioFormat::Float)
        ->setChannelCount(oboe::ChannelCount::Mono)
        // The engine runs at a fixed rate; Oboe resamples if the device differs.
        ->setSampleRate(kSampleRate)
        ->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium)
        ->setDataCallback(this)
        ->setErrorCallback(this);

    oboe::Result result = builder.openStream(stream_);
    if (result != oboe::Result::OK) {
        LOGE("openStream failed: %s", oboe::convertToText(result));
        stream_.reset();
        return false;
    }
    // Two bursts of buffering: low latency with some margin against underruns.
    stream_->setBufferSizeInFrames(stream_->getFramesPerBurst() * 2);
    result = stream_->requestStart();
    if (result != oboe::Result::OK) {
        LOGE("requestStart failed: %s", oboe::convertToText(result));
        stream_->close();
        stream_.reset();
        return false;
    }
    LOGI("Stream open: %d Hz, burst %d frames, %s", stream_->getSampleRate(),
         stream_->getFramesPerBurst(), stream_->getSharingMode() == oboe::SharingMode::Exclusive
                                           ? "exclusive" : "shared");
    return true;
}

void AudioPlayer::close() {
    wantOpen_.store(false);
    std::lock_guard<std::mutex> lock(streamMutex_);
    if (stream_) {
        stream_->stop();
        stream_->close();
        stream_.reset();
    }
}

void AudioPlayer::applyRequests() {
    const uint32_t tempo = requestedTempo_.load(std::memory_order_relaxed);
    if (tempo != tempo_) {
        tempo_ = tempo;
        engine_->clock().setTempoMilliBpm(tempo);
    }

    const int root = requestedRoot_.load(std::memory_order_relaxed);
    const int scale = requestedScale_.load(std::memory_order_relaxed);
    if (root != root_ || scale != scale_) {
        root_ = root;
        scale_ = scale;
        const int r = ((root % 12) + 12) % 12;
        const int s = (scale >= 0 && scale < kNumScales) ? scale : 0;
        engine_->pitch().current = adaptive::Key{static_cast<uint8_t>(r), kScales[s]};
    }

    const bool playing = requestedPlaying_.load(std::memory_order_relaxed);
    if (playing != playing_) {
        playing_ = playing;
        if (playing) {
            song_->start(*engine_, engine_->nextTick());
        } else {
            song_->stop(*engine_);
        }
    }
}

oboe::DataCallbackResult AudioPlayer::onAudioReady(oboe::AudioStream*, void* audioData, int32_t numFrames) {
    applyRequests();
    engine_->render(static_cast<float*>(audioData), static_cast<uint32_t>(numFrames));
    return oboe::DataCallbackResult::Continue;
}

void AudioPlayer::onErrorAfterClose(oboe::AudioStream*, oboe::Result error) {
    // Typically the output device changed (headphones unplugged). Reopen,
    // unless the app closed the stream meanwhile.
    LOGI("Stream closed (%s)", oboe::convertToText(error));
    std::lock_guard<std::mutex> lock(streamMutex_);
    stream_.reset();
    if (wantOpen_.load()) openLocked();
}
