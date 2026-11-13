#include "audio_player.hpp"

#include <android/log.h>

#include <chrono>


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
      song_(std::make_unique<adaptive::demo::AdaptiveDemo>()) {
    song_->setUp(*engine_);
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
    // Start at two bursts; the tuner adds a burst after each underrun.
    const int32_t burst = stream_->getFramesPerBurst();
    stream_->setBufferSizeInFrames(burst * 2);
    latencyTuner_ = std::make_unique<oboe::LatencyTuner>(*stream_);
    latencyTuner_->setMinimumBufferSize(burst * 2);
    // Lets the OS raise CPU speed when callbacks get close to their deadline.
    stream_->setPerformanceHintEnabled(true);
    underruns_.store(0);
    burstFrames_.store(burst);
    result = stream_->requestStart();
    if (result != oboe::Result::OK) {
        LOGE("requestStart failed: %s", oboe::convertToText(result));
        stream_->close();
        stream_.reset();
        latencyTuner_.reset();
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
        latencyTuner_.reset();
    }
}

bool AudioPlayer::play() {
    using adaptive::demo::AdaptiveDemo;
    return engine_->post(adaptive::Command::gotoSection(AdaptiveDemo::kExplore, adaptive::Quantize::Now));
}

bool AudioPlayer::stop() { return engine_->post(adaptive::Command::stop()); }

bool AudioPlayer::gotoSection(int section, int quantize, int entry, bool viaBridge) {
    using adaptive::demo::AdaptiveDemo;
    const auto q = static_cast<adaptive::Quantize>(quantize < 0 || quantize > 2 ? 2 : quantize);
    const auto e = entry == 1 ? adaptive::Entry::KeepPhase : adaptive::Entry::Restart;
    const uint8_t bridge = viaBridge ? AdaptiveDemo::kBridge : adaptive::kNoIndex;
    return engine_->post(adaptive::Command::gotoSection(static_cast<uint8_t>(section), q, e, bridge));
}

bool AudioPlayer::fireEvent(int event) {
    return engine_->post(adaptive::Command::fireEvent(static_cast<uint8_t>(event)));
}

bool AudioPlayer::setLayerMute(int layer, bool muted) {
    return engine_->post(adaptive::Command::setLayerMute(static_cast<uint8_t>(layer), muted));
}

bool AudioPlayer::setParam(int param, float value) {
    return engine_->post(adaptive::Command::setParam(static_cast<uint8_t>(param), value));
}

bool AudioPlayer::setKey(int root, int scaleIndex) {
    const int r = ((root % 12) + 12) % 12;
    const int s = (scaleIndex >= 0 && scaleIndex < kNumScales) ? scaleIndex : 0;
    return engine_->post(adaptive::Command::setKey(static_cast<uint8_t>(r), kScales[s], adaptive::Quantize::Bar));
}

oboe::DataCallbackResult AudioPlayer::onAudioReady(oboe::AudioStream* stream, void* audioData,
                                                   int32_t numFrames) {
    const auto t0 = std::chrono::steady_clock::now();
    if (latencyTuner_) latencyTuner_->tune();
    engine_->render(static_cast<float*>(audioData), static_cast<uint32_t>(numFrames));
    const int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now() - t0).count();
    updateStats(stream, ns, numFrames);
    return oboe::DataCallbackResult::Continue;
}

void AudioPlayer::updateStats(oboe::AudioStream* stream, int64_t callbackNs, int32_t numFrames) {
    if (resetStatsRequested_.exchange(false)) {
        maxCallbackUs_.store(0);
    }
    const int32_t us = static_cast<int32_t>(callbackNs / 1000);
    if (us > maxCallbackUs_.load(std::memory_order_relaxed)) {
        maxCallbackUs_.store(us, std::memory_order_relaxed);
        callbackBudgetUs_.store(static_cast<int32_t>(int64_t(numFrames) * 1000000 / kSampleRate),
                                std::memory_order_relaxed);
    }
    bufferFrames_.store(stream->getBufferSizeInFrames(), std::memory_order_relaxed);

    const auto xruns = stream->getXRunCount();
    if (xruns) underruns_.store(xruns.value(), std::memory_order_relaxed);
    const uint8_t section = engine_->currentSection();
    currentSection_.store(section == adaptive::kNoIndex ? -1 : section, std::memory_order_relaxed);
}

AudioPlayer::Stats AudioPlayer::stats() const {
    return Stats{underruns_.load(), bufferFrames_.load(), burstFrames_.load(),
                 maxCallbackUs_.load(), callbackBudgetUs_.load(), currentSection_.load()};
}

void AudioPlayer::onErrorAfterClose(oboe::AudioStream*, oboe::Result error) {
    // Typically the output device changed (headphones unplugged). Reopen,
    // unless the app closed the stream meanwhile.
    LOGI("Stream closed (%s)", oboe::convertToText(error));
    std::lock_guard<std::mutex> lock(streamMutex_);
    stream_.reset();
    latencyTuner_.reset();
    if (wantOpen_.load()) openLocked();
}
