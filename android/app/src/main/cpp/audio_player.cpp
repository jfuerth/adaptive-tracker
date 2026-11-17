#include "audio_player.hpp"

#include <android/log.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <type_traits>


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
      song_(std::make_unique<adaptive::Song>()),
      edit_(std::make_unique<adaptive::Song>()) {
    engine_->setSong(song_.get());
    resetPositions();
}

void AudioPlayer::resetPositions() {
    for (auto& a : playingPattern_) a.store(-1);
    for (auto& a : playingRow_) a.store(-1);
    for (auto& a : stripFlags_) a.store(0);
}

std::string AudioPlayer::loadSong(std::string_view json) {
    auto song = std::make_unique<adaptive::Song>();
    adaptive::songfile::SongInfo info;
    const auto result = adaptive::songfile::load(json, *song, info);
    if (!result.ok) return result.error.empty() ? "unknown error" : result.error;

    // The audio thread must not see the swap: stop it, swap, start again.
    const bool wasOpen = wantOpen_.load();
    close();
    engine_ = std::make_unique<adaptive::Engine>(kSampleRate);
    swaps_.clear();
    swapsFreed_ = 0;
    undo_.clear();
    redo_.clear();
    *edit_ = *song;
    song_ = std::move(song);
    info_ = std::move(info);
    engine_->setSong(song_.get());
    resetPositions();
    edits_ = 0;
    if (wasOpen) open();
    LOGI("Loaded song \"%s\": %u channels, %u patterns, %u sections", info_.title.c_str(),
         edit_->numChannels, edit_->numPatterns, edit_->numSections);
    return "";
}

std::string AudioPlayer::saveSong() const { return adaptive::songfile::save(*edit_, info_); }

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

bool AudioPlayer::setHearAll(bool on) { return engine_->post(adaptive::Command::hearAll(on)); }

bool AudioPlayer::play(bool hearAll) {
    if (edit_->numSections == 0) return false;
    return setHearAll(hearAll) && engine_->post(adaptive::Command::gotoSection(edit_->startSection, adaptive::Quantize::Now));
}

bool AudioPlayer::stop() { return engine_->post(adaptive::Command::stop()); }

int AudioPlayer::firstBridge() const {
    for (uint8_t i = 0; i < edit_->numSections; ++i) {
        if (edit_->sections[i].bridgeBeats > 0) return i;
    }
    return -1;
}

bool AudioPlayer::gotoSection(int section, int quantize, int entry, bool viaBridge) {
    const auto q = static_cast<adaptive::Quantize>(quantize < 0 || quantize > 2 ? 2 : quantize);
    const auto e = entry == 1 ? adaptive::Entry::KeepPhase : adaptive::Entry::Restart;
    const int b = viaBridge ? firstBridge() : -1;
    const uint8_t bridge = b >= 0 ? static_cast<uint8_t>(b) : adaptive::kNoIndex;
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

const uint16_t* AudioPlayer::scaleMasks(int* count) {
    *count = kNumScales;
    return kScales;
}

bool AudioPlayer::setKey(int root, int scale) {
    const int r = ((root % 12) + 12) % 12;
    const uint16_t mask = static_cast<uint16_t>(scale & 0xFFF);
    if ((mask & 1) == 0) return false;  // a scale always contains its root
    return engine_->post(adaptive::Command::setKey(static_cast<uint8_t>(r), mask, adaptive::Quantize::Bar));
}

bool AudioPlayer::patternInfo(uint32_t i, int32_t out[5]) {
    if (i >= edit_->numPatterns) return false;
    const adaptive::Pattern& p = edit_->patterns[i];
    const adaptive::ChannelSetup& ch = edit_->channels[p.channel];
    out[0] = p.channel;
    out[1] = p.numRows;
    out[2] = p.ticksPerRow;
    out[3] = ch.numColumns;
    out[4] = static_cast<int32_t>(ch.pitchModes[0]);
    return true;
}

bool AudioPlayer::readPattern(uint32_t i, uint8_t* out, uint32_t capacity) {
    if (i >= edit_->numPatterns) return false;
    const adaptive::Pattern& p = edit_->patterns[i];
    const uint32_t columns = edit_->channels[p.channel].numColumns;
    if (capacity < p.numRows * columns * 5u) return false;
    for (uint32_t r = 0; r < p.numRows; ++r) {
        for (uint32_t c = 0; c < columns; ++c) {
            const adaptive::Cell& cell = p.cells[r][c];
            *out++ = cell.note;
            *out++ = static_cast<uint8_t>(cell.accidental);
            *out++ = cell.volume;
            *out++ = cell.effect;
            *out++ = cell.effectParam;
        }
    }
    return true;
}

bool AudioPlayer::setCell(uint32_t pattern, int row, int column, const adaptive::Cell& cell) {
    if (pattern >= edit_->numPatterns || row < 0 || row >= edit_->patterns[pattern].numRows || column < 0 ||
        column >= static_cast<int>(adaptive::config::kMaxColumns)) {
        return false;
    }
    checkpoint("Edit cell", Merge::Cell, pattern, row, column);
    if (!engine_->post(adaptive::Command::setCell(static_cast<uint8_t>(pattern), static_cast<uint16_t>(row),
                                                  static_cast<uint8_t>(column), cell))) {
        return false;
    }
    edit_->patterns[pattern].cells[row][column] = cell;
    ++edits_;
    return true;
}

// Each shape edit goes to the engine and is made to the UI's copy by the same
// Song method, so the two agree (the engine ignores what that method rejects).
bool AudioPlayer::setPatternShape(uint32_t pattern, int rows, int ticksPerRow) {
    if (rows < 1 || rows > 0xFFFF || ticksPerRow < 1 || ticksPerRow > 255) return false;
    checkpoint("Change pattern length");
    if (!engine_->post(adaptive::Command::setPatternShape(static_cast<uint8_t>(pattern), static_cast<uint16_t>(rows),
                                                          static_cast<uint8_t>(ticksPerRow)))) {
        return false;
    }
    if (!edit_->setPatternShape(pattern, rows, ticksPerRow)) return false;
    ++edits_;
    return true;
}

bool AudioPlayer::clearCells(uint32_t pattern, int column, int first, int last, int parts) {
    if (column < 0 || column > 255 || first < 0 || last < first || last > 255 || parts < 0 || parts > 255) {
        return false;
    }
    const auto bits = static_cast<uint8_t>(parts);
    checkpoint("Clear cells");
    if (!engine_->post(adaptive::Command::clearCells(static_cast<uint8_t>(pattern), static_cast<uint8_t>(column),
                                                     static_cast<uint32_t>(first), static_cast<uint32_t>(last), bits))) {
        return false;
    }
    if (!edit_->clearCells(pattern, column, first, last, bits)) return false;
    ++edits_;
    return true;
}

bool AudioPlayer::shiftRows(uint32_t pattern, int columnMask, int row, bool down) {
    if (columnMask <= 0 || columnMask > 255 || row < 0 || row > 255) return false;
    // Taps at one place merge into one step, inserts and pulls alike.
    checkpoint(down ? "Insert row" : "Pull up rows", Merge::Shift, pattern, static_cast<uint32_t>(columnMask),
               static_cast<uint32_t>(row));
    const auto mask = static_cast<uint8_t>(columnMask);
    if (!engine_->post(adaptive::Command::shiftRows(static_cast<uint8_t>(pattern), mask, static_cast<uint32_t>(row), down))) {
        return false;
    }
    if (!edit_->shiftRows(pattern, mask, static_cast<uint32_t>(row), down)) return false;
    ++edits_;
    return true;
}

bool AudioPlayer::scaleRows(uint32_t pattern, int num, int den) {
    if (num < 1 || num > 255 || den < 1 || den > 255) return false;
    checkpoint("Stretch or squash");
    if (!engine_->post(adaptive::Command::scaleRows(static_cast<uint8_t>(pattern), static_cast<uint8_t>(num),
                                                    static_cast<uint16_t>(den)))) {
        return false;
    }
    if (!edit_->scaleRows(pattern, num, den)) return false;
    ++edits_;
    return true;
}

bool AudioPlayer::setColumns(int channel, int count) {
    if (channel < 0 || channel > 255 || count < 0 || count > 255) return false;
    checkpoint("Add column");
    if (!engine_->post(adaptive::Command::setColumns(static_cast<uint8_t>(channel), static_cast<uint8_t>(count)))) {
        return false;
    }
    if (!edit_->setColumns(channel, count)) return false;
    ++edits_;
    return true;
}

bool AudioPlayer::removeColumn(int channel, int column) {
    if (channel < 0 || channel > 255 || column < 0 || column > 255) return false;
    checkpoint("Remove column");
    if (!engine_->post(adaptive::Command::removeColumn(static_cast<uint8_t>(channel), static_cast<uint8_t>(column)))) {
        return false;
    }
    if (!edit_->removeColumn(channel, column)) return false;
    ++edits_;
    return true;
}

// New patterns, channels and sections are written just past the end of
// their table, where the audio thread doesn't look, then announced.

namespace {
/// Labels go index for index; make room for `index` and label it.
void label(std::vector<adaptive::songfile::Label>& labels, uint32_t index, const std::string& name) {
    labels.resize(index);
    labels.push_back({adaptive::songfile::makeId(name, labels), name});
}
} // namespace

int AudioPlayer::addPattern(int channel, const std::string& name, int copyOf) {
    const uint32_t i = edit_->numPatterns;
    if (i >= adaptive::config::kMaxPatterns || channel < 0 || channel >= static_cast<int>(edit_->numChannels) ||
        copyOf >= static_cast<int>(edit_->numPatterns)) {
        return -1;
    }
    adaptive::Pattern p;
    if (copyOf >= 0) {
        p = edit_->patterns[copyOf];
        edit_->adaptPattern(p, p.channel, static_cast<uint32_t>(channel));  // notes written as the channel writes them
    }
    p.channel = static_cast<uint8_t>(channel);
    checkpoint("New pattern");
    live()->patterns[i] = p;
    if (!engine_->post(adaptive::Command::added(adaptive::Addition::Pattern, static_cast<uint8_t>(i)))) return -1;
    edit_->patterns[i] = p;
    edit_->numPatterns = static_cast<uint8_t>(i + 1);
    label(info_.patterns, i, name);
    ++edits_;
    return static_cast<int>(i);
}

int AudioPlayer::addChannel(const std::string& name, int mode, int instrument) {
    const uint32_t i = edit_->numChannels;
    if (i >= adaptive::config::kMaxChannels || mode < 0 || mode > 2) return -1;
    adaptive::ChannelSetup c;
    for (auto& m : c.pitchModes) m = static_cast<adaptive::PitchMode>(mode);
    c.numColumns = 1;
    if (instrument >= 0 && instrument < static_cast<int>(edit_->numInstruments)) {
        c.instruments[0] = static_cast<uint8_t>(instrument);
    }
    checkpoint("New channel");
    live()->channels[i] = c;
    if (!engine_->post(adaptive::Command::added(adaptive::Addition::Channel, static_cast<uint8_t>(i)))) return -1;
    edit_->channels[i] = c;
    edit_->numChannels = static_cast<uint8_t>(i + 1);
    label(info_.channels, i, name);
    ++edits_;
    return static_cast<int>(i);
}

int AudioPlayer::addSection(const std::string& name) {
    const uint32_t i = edit_->numSections;
    if (i >= adaptive::config::kMaxSections) return -1;
    checkpoint("New section");
    live()->sections[i] = adaptive::Section();
    if (!engine_->post(adaptive::Command::added(adaptive::Addition::Section, static_cast<uint8_t>(i)))) return -1;
    edit_->addSection();
    label(info_.sections, i, name);
    ++edits_;
    return static_cast<int>(i);
}

bool AudioPlayer::rename(int kind, uint32_t index, const std::string& name) {
    std::vector<adaptive::songfile::Label>* labels = nullptr;
    uint32_t count = 0;
    switch (kind) {
    case 0: labels = &info_.sections; count = edit_->numSections; break;
    case 1: labels = &info_.layers; count = edit_->numLayers; break;
    case 2: labels = &info_.params; count = edit_->numParams; break;
    case 3: labels = &info_.events; count = edit_->numEvents; break;
    case 4: labels = &info_.channels; count = edit_->numChannels; break;
    case 5: labels = &info_.patterns; count = edit_->numPatterns; break;
    default: return false;
    }
    if (index >= count || index >= labels->size() || name.empty()) return false;
    checkpoint("Rename");
    (*labels)[index].name = name;
    ++edits_;
    return true;
}

bool AudioPlayer::editSequence(int section, int channel, int op, int pos, int pattern) {
    if (section < 0 || section > 255 || channel < 0 || channel > 255 || op < 0 || op > 2 || pos < 0 || pos > 255 ||
        pattern < 0 || pattern > 255) {
        return false;
    }
    const auto edit = static_cast<adaptive::SeqEdit>(op);
    checkpoint("Change sequence");
    if (!engine_->post(adaptive::Command::editSequence(static_cast<uint8_t>(section), static_cast<uint8_t>(channel),
                                                       edit, static_cast<uint8_t>(pos), static_cast<uint8_t>(pattern)))) {
        return false;
    }
    if (!edit_->editSequence(section, channel, edit, pos, pattern)) return false;
    ++edits_;
    return true;
}

bool AudioPlayer::setSection(int section, float bpm, int beatsPerBar, int bridgeBeats) {
    if (section < 0 || section > 255 || beatsPerBar < 1 || beatsPerBar > 32 || bridgeBeats < 0 ||
        bridgeBeats > 0xFFFF || !(bpm >= 0.0f) || bpm > 999.0f) {
        return false;
    }
    checkpoint("Section timing", Merge::Section, section);
    if (!engine_->post(adaptive::Command::setSection(static_cast<uint8_t>(section), bpm,
                                                     static_cast<uint8_t>(beatsPerBar),
                                                     static_cast<uint16_t>(bridgeBeats)))) {
        return false;
    }
    if (!edit_->setSectionTiming(section, static_cast<uint32_t>(bpm * 1000.0f + 0.5f), beatsPerBar, bridgeBeats)) {
        return false;
    }
    ++edits_;
    return true;
}

std::vector<int32_t> AudioPlayer::columnSettings(int channel) const {
    std::vector<int32_t> out;
    if (channel < 0 || channel >= static_cast<int>(edit_->numChannels)) return out;
    const adaptive::ChannelSetup& c = edit_->channels[channel];
    for (uint32_t col = 0; col < c.numColumns; ++col) {
        out.push_back(static_cast<int32_t>(c.pitchModes[col]));
        out.push_back(c.gateParams[col] < edit_->numParams ? c.gateParams[col] : -1);
        out.push_back(static_cast<int32_t>(c.gateThresholds[col] * 1000.0f + 0.5f));
    }
    return out;
}

bool AudioPlayer::setColumnPitch(int channel, int column, int mode) {
    if (channel < 0 || channel > 255 || column < 0 || column > 255 || mode < 0 || mode > 2) return false;
    const auto m = static_cast<adaptive::PitchMode>(mode);
    checkpoint("Column pitch mode");
    if (!engine_->post(adaptive::Command::setColumnPitch(static_cast<uint8_t>(channel), static_cast<uint8_t>(column), m))) {
        return false;
    }
    if (!edit_->setColumnPitch(channel, column, m)) return false;
    ++edits_;
    return true;
}

bool AudioPlayer::setColumnGate(int channel, int column, int param, float threshold) {
    if (channel < 0 || channel > 255 || column < 0 || column > 255 || param < -1 || param > 254) return false;
    const uint8_t p = param < 0 ? adaptive::kNoIndex : static_cast<uint8_t>(param);
    checkpoint("Column threshold", Merge::Gate, channel, column);
    if (!engine_->post(adaptive::Command::setColumnGate(static_cast<uint8_t>(channel), static_cast<uint8_t>(column), p,
                                                        threshold))) {
        return false;
    }
    if (!edit_->setColumnGate(channel, column, p, threshold)) return false;
    ++edits_;
    return true;
}

int AudioPlayer::addLayer(const std::string& name) {
    const uint32_t i = edit_->numLayers;
    if (i >= adaptive::config::kMaxLayers) return -1;
    checkpoint("New layer");
    live()->layers[i] = 0;
    if (!engine_->post(adaptive::Command::added(adaptive::Addition::Layer, static_cast<uint8_t>(i)))) return -1;
    edit_->addLayer();
    label(info_.layers, i, name);
    ++edits_;
    return static_cast<int>(i);
}

bool AudioPlayer::removeLayer(int layer) {
    if (layer < 0 || layer >= static_cast<int>(edit_->numLayers)) return false;
    checkpoint("Remove layer");
    if (!engine_->post(adaptive::Command::removeLayer(static_cast<uint8_t>(layer)))) return false;
    adaptive::Renumbering r;
    edit_->removeLayer(layer, &r);
    undo_.back().renumbering = undo_.back().renumbering.then(r);
    if (layer < static_cast<int>(info_.layers.size())) info_.layers.erase(info_.layers.begin() + layer);
    ++edits_;
    return true;
}

bool AudioPlayer::setLayerColumns(int layer, int channel, int columnBits) {
    if (layer < 0 || layer > 255 || channel < 0 || channel > 255 || columnBits < 0 || columnBits > 0xFFFF) return false;
    checkpoint("Layer columns");
    if (!engine_->post(adaptive::Command::setLayerColumns(static_cast<uint8_t>(layer), static_cast<uint8_t>(channel),
                                                          static_cast<uint16_t>(columnBits)))) {
        return false;
    }
    if (!edit_->setLayerColumns(layer, channel, columnBits)) return false;
    ++edits_;
    return true;
}

int AudioPlayer::instrumentPitch(int instrument) const {
    if (instrument < 0 || instrument >= static_cast<int>(edit_->numInstruments)) return -1;
    const uint8_t m = edit_->instrumentPitch[instrument];
    return m <= static_cast<uint8_t>(adaptive::PitchMode::Degree) ? m : -1;
}

bool AudioPlayer::setInstrumentPitch(int instrument, int mode) {
    if (instrument < 0 || instrument >= static_cast<int>(edit_->numInstruments) || mode < -1 || mode > 2) return false;
    checkpoint("Instrument note hint");
    edit_->instrumentPitch[instrument] = mode < 0 ? adaptive::kNoIndex : static_cast<uint8_t>(mode);
    ++edits_;
    return true;
}

int AudioPlayer::addInstrument(const float* values, const std::string& name) {
    if (edit_->numInstruments >= adaptive::config::kMaxInstruments) return -1;
    const uint32_t i = edit_->numInstruments;
    adaptive::Instrument inst;
    if (values != nullptr) {
        for (uint32_t p = 0; p < adaptive::Instrument::kNumParams; ++p) {
            inst.set(static_cast<adaptive::Instrument::Param>(p), values[p]);
        }
    }
    // Not yet in use, so the audio thread doesn't read it until AddInstrument.
    checkpoint("New instrument");
    live()->instruments[i] = inst;
    if (!engine_->post(adaptive::Command::addInstrument(static_cast<uint8_t>(i)))) return -1;
    edit_->addInstrument(inst);
    info_.instruments.resize(i);  // labels go index for index with instruments
    info_.instruments.push_back({adaptive::songfile::makeId(name, info_.instruments), name});
    ++edits_;
    return static_cast<int>(i);
}

bool AudioPlayer::renameInstrument(uint32_t i, const std::string& name) {
    if (i >= edit_->numInstruments || i >= info_.instruments.size() || name.empty()) return false;
    checkpoint("Rename instrument");
    info_.instruments[i].name = name;
    ++edits_;
    return true;
}

bool AudioPlayer::assignInstrument(int channel, int column, int instrument) {
    if (channel < 0 || channel >= static_cast<int>(adaptive::config::kMaxChannels) || column < 0 ||
        column >= static_cast<int>(edit_->channels[channel].numColumns) ||
        instrument < 0 || instrument >= static_cast<int>(edit_->numInstruments)) {
        return false;
    }
    checkpoint("Assign instrument");
    if (!engine_->post(adaptive::Command::assignInstrument(static_cast<uint8_t>(channel),
                                                          static_cast<uint8_t>(column),
                                                          static_cast<uint8_t>(instrument)))) {
        return false;
    }
    edit_->channels[channel].instruments[column] = static_cast<uint8_t>(instrument);
    ++edits_;
    return true;
}

bool AudioPlayer::setMonitor(int instrument) {
    const uint8_t i = instrument < 0 || instrument >= static_cast<int>(edit_->numInstruments)
                          ? adaptive::kNoIndex : static_cast<uint8_t>(instrument);
    return engine_->post(adaptive::Command::setMonitor(i));
}

void AudioPlayer::readMonitor(float* out, uint32_t n) const {
    if (n > kMonitorFrames) n = kMonitorFrames;
    const uint32_t end = monitorWrite_.load(std::memory_order_acquire);
    for (uint32_t k = 0; k < n; ++k) out[k] = monitorRing_[(end - n + k) % kMonitorFrames];
}

bool AudioPlayer::readInstrument(uint32_t i, float* out, uint32_t capacity) const {
    const adaptive::Song& song = *edit_;
    if (i >= song.numInstruments || capacity < adaptive::Instrument::kNumParams) return false;
    for (uint32_t p = 0; p < adaptive::Instrument::kNumParams; ++p) out[p] = song.instruments[i].values[p];
    return true;
}

bool AudioPlayer::setInstrumentParam(uint32_t i, int param, float value) {
    if (i >= edit_->numInstruments || param < 0 || param >= adaptive::Instrument::kNumParams) return false;
    const auto p = static_cast<adaptive::Instrument::Param>(param);
    checkpoint("Instrument setting", Merge::Instrument, i, static_cast<uint32_t>(param));
    if (!engine_->post(adaptive::Command::setInstrumentParam(static_cast<uint8_t>(i), p, value))) return false;
    edit_->instruments[i].set(p, value);
    ++edits_;
    return true;
}

int AudioPlayer::columnInstrument(int channel, int column) const {
    if (channel < 0 || channel >= static_cast<int>(adaptive::config::kMaxChannels) || column < 0 ||
        column >= static_cast<int>(edit_->channels[channel].numColumns)) {
        return -1;
    }
    const uint8_t i = edit_->channels[channel].instruments[column];
    return i == adaptive::kNoIndex ? -1 : i;
}

bool AudioPlayer::previewOn(int instrument, int slot, int midiNote) {
    if (midiNote < 0 || midiNote > 127 || instrument < 0) return false;
    return engine_->post(adaptive::Command::previewOn(static_cast<uint8_t>(instrument), static_cast<uint8_t>(slot),
                                                     static_cast<uint8_t>(midiNote), 0.8f));
}

bool AudioPlayer::soloPattern(uint32_t pattern, int columnMask, bool hearAll) {
    if (edit_->pattern(pattern) == nullptr) return false;
    return setHearAll(hearAll) && engine_->post(adaptive::Command::soloPattern(edit_->patterns[pattern].channel, static_cast<uint8_t>(pattern),
                                                       static_cast<uint16_t>(columnMask)));
}

bool AudioPlayer::setColumnMask(int channel, int columnMask) {
    return engine_->post(adaptive::Command::setColumnMask(static_cast<uint8_t>(channel),
                                                         static_cast<uint16_t>(columnMask)));
}

bool AudioPlayer::previewOff(int slot) {
    return engine_->post(adaptive::Command::previewOff(static_cast<uint8_t>(slot)));
}

void AudioPlayer::homeKey(int32_t out[2]) const {
    // Set when the song loads, before audio starts, and never changed.
    out[0] = engine_->pitch().home.root;
    out[1] = engine_->pitch().home.scale;
}

void AudioPlayer::playPosition(int channel, int32_t out[2]) const {
    if (channel < 0 || channel >= static_cast<int>(adaptive::config::kMaxChannels)) {
        out[0] = out[1] = -1;
        return;
    }
    out[0] = playingPattern_[channel].load(std::memory_order_relaxed);
    out[1] = playingRow_[channel].load(std::memory_order_relaxed);
}

void AudioPlayer::currentKey(int32_t out[2]) const {
    out[0] = keyRoot_.load(std::memory_order_relaxed);
    out[1] = keyScale_.load(std::memory_order_relaxed);
}

oboe::DataCallbackResult AudioPlayer::onAudioReady(oboe::AudioStream* stream, void* audioData,
                                                   int32_t numFrames) {
    const auto t0 = std::chrono::steady_clock::now();
    if (latencyTuner_) latencyTuner_->tune();
    // Render in pieces that fit the monitor scratch buffer, copying each
    // piece's monitor output into the ring.
    float* out = static_cast<float*>(audioData);
    uint32_t write = monitorWrite_.load(std::memory_order_relaxed);
    for (int32_t done = 0; done < numFrames;) {
        const uint32_t n = std::min<uint32_t>(static_cast<uint32_t>(numFrames - done), kMonitorScratch);
        engine_->render(out + done, n, monitorScratch_);
        for (uint32_t k = 0; k < n; ++k) monitorRing_[(write + k) % kMonitorFrames] = monitorScratch_[k];
        write += n;
        done += static_cast<int32_t>(n);
    }
    monitorWrite_.store(write, std::memory_order_release);
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
    playing_.store(engine_->isPlaying() ? 1 : 0, std::memory_order_relaxed);

    for (uint32_t c = 0; c < adaptive::config::kMaxChannels; ++c) {
        const adaptive::Channel& ch = engine_->channel(c);
        const adaptive::Pattern* p = ch.playingPattern();
        int32_t index = -1;
        const adaptive::Song* song = engine_->song();
        if (p != nullptr && song != nullptr && p >= song->patterns && p < song->patterns + song->numPatterns) {
            index = static_cast<int32_t>(p - song->patterns);
        }
        playingPattern_[c].store(index, std::memory_order_relaxed);
        playingRow_[c].store(ch.playingRow(), std::memory_order_relaxed);
        stripFlags_[c].store((ch.stripMuted() ? 1 : 0) | (ch.soloed() ? 2 : 0) | (ch.paused() ? 4 : 0),
                             std::memory_order_relaxed);
    }
    keyRoot_.store(engine_->pitch().current.root, std::memory_order_relaxed);
    keyScale_.store(engine_->pitch().current.scale, std::memory_order_relaxed);
}

AudioPlayer::Stats AudioPlayer::stats() const {
    return Stats{underruns_.load(), bufferFrames_.load(), burstFrames_.load(),
                 maxCallbackUs_.load(), callbackBudgetUs_.load(), currentSection_.load(),
                 playing_.load()};
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

// ---- Swaps, deletes and undo ----

void AudioPlayer::collectSwaps() {
    const uint32_t taken = engine_->swapsTaken();
    while (!swaps_.empty() && swapsFreed_ + 1 < taken) {
        swaps_.pop_front();
        ++swapsFreed_;
    }
    if (taken > 0) song_.reset();
}

bool AudioPlayer::swapIn(const adaptive::Song& song, const adaptive::Renumbering& r) {
    collectSwaps();
    auto swap = std::make_unique<adaptive::SongSwap>();
    swap->song = song;
    swap->renumbering = r;
    if (!engine_->offer(swap.get())) return false;
    swaps_.push_back(std::move(swap));
    return true;
}

namespace {
constexpr size_t kPatternsAt = offsetof(adaptive::Song, patterns);
constexpr size_t kPatternsEnd = kPatternsAt + sizeof(adaptive::Song::patterns);
static_assert(std::is_trivially_copyable_v<adaptive::Song> && std::is_standard_layout_v<adaptive::Song>,
              "undo snapshots copy Song's bytes");
} // namespace

AudioPlayer::Snapshot AudioPlayer::capture() const {
    Snapshot snap;
    const auto* bytes = reinterpret_cast<const uint8_t*>(edit_.get());
    snap.rest.assign(bytes, bytes + kPatternsAt);
    snap.rest.insert(snap.rest.end(), bytes + kPatternsEnd, bytes + sizeof(adaptive::Song));
    // Share patterns that haven't changed since the last snapshot.
    const Snapshot* last = undo_.empty() ? nullptr : &undo_.back().before;
    for (uint32_t i = 0; i < edit_->numPatterns; ++i) {
        const adaptive::Pattern& p = edit_->patterns[i];
        if (last != nullptr && i < last->patterns.size() &&
            std::memcmp(last->patterns[i].get(), &p, sizeof p) == 0) {
            snap.patterns.push_back(last->patterns[i]);
        } else {
            snap.patterns.push_back(std::make_shared<const adaptive::Pattern>(p));
        }
    }
    snap.info = info_;
    return snap;
}

void AudioPlayer::restore(const Snapshot& snap, adaptive::Song& song) {
    auto* bytes = reinterpret_cast<uint8_t*>(&song);
    std::memcpy(bytes, snap.rest.data(), kPatternsAt);
    std::memcpy(bytes + kPatternsEnd, snap.rest.data() + kPatternsAt, sizeof(adaptive::Song) - kPatternsEnd);
    for (uint32_t i = 0; i < adaptive::config::kMaxPatterns; ++i) {
        song.patterns[i] = i < snap.patterns.size() ? *snap.patterns[i] : adaptive::Pattern();
    }
}

void AudioPlayer::checkpoint(const char* label, Merge merge, uint32_t a, uint32_t b, uint32_t c) {
    if (stepDepth_ > 0) {
        if (stepTaken_) return;
        stepTaken_ = true;
        label = stepLabel_.c_str();
        merge = Merge::None;
    } else if (merge != Merge::None && redo_.empty() && !undo_.empty()) {
        const Step& top = undo_.back();
        if (top.merge == merge && top.target[0] == a && top.target[1] == b && top.target[2] == c) return;
    }
    Step step;
    step.before = capture();
    step.label = label;
    step.merge = merge;
    step.target[0] = a;
    step.target[1] = b;
    step.target[2] = c;
    undo_.push_back(std::move(step));
    if (undo_.size() > kMaxUndo) undo_.pop_front();
    redo_.clear();
}

void AudioPlayer::beginStep(const std::string& label) {
    if (stepDepth_++ == 0) {
        stepTaken_ = false;
        stepLabel_ = label;
    }
}

void AudioPlayer::endStep() {
    if (stepDepth_ > 0) --stepDepth_;
}

bool AudioPlayer::undo() {
    if (undo_.empty()) return false;
    Step& top = undo_.back();
    auto restored = std::make_unique<adaptive::Song>();
    restore(top.before, *restored);
    if (!swapIn(*restored, top.renumbering.inverse())) return false;
    Step redoStep;
    redoStep.before = capture();  // the song as it is now, to come back to
    redoStep.renumbering = top.renumbering;
    redoStep.label = top.label;
    *edit_ = *restored;
    info_ = top.before.info;
    redo_.push_back(std::move(redoStep));
    undo_.pop_back();
    ++edits_;
    return true;
}

bool AudioPlayer::redo() {
    if (redo_.empty()) return false;
    Step& top = redo_.back();
    auto restored = std::make_unique<adaptive::Song>();
    restore(top.before, *restored);
    if (!swapIn(*restored, top.renumbering)) return false;
    Step undoStep;
    undoStep.before = capture();
    undoStep.renumbering = top.renumbering;
    undoStep.label = top.label;
    *edit_ = *restored;
    info_ = top.before.info;
    undo_.push_back(std::move(undoStep));
    redo_.pop_back();
    ++edits_;
    return true;
}

template<typename Fn>
bool AudioPlayer::removeAndSwap(const char* label, Fn&& remove) {
    auto next = std::make_unique<adaptive::Song>(*edit_);
    adaptive::Renumbering r;
    if (!remove(*next, r) || !swapIn(*next, r)) return false;
    checkpoint(label);
    undo_.back().renumbering = undo_.back().renumbering.then(r);  // a step may hold several
    *edit_ = *next;
    ++edits_;
    return true;
}

namespace {
template<typename T>
void eraseAt(std::vector<T>& v, uint32_t i) {
    if (i < v.size()) v.erase(v.begin() + i);
}
} // namespace

bool AudioPlayer::deletePattern(int pattern) {
    if (pattern < 0 || pattern >= static_cast<int>(edit_->numPatterns)) return false;
    const auto p = static_cast<uint32_t>(pattern);
    if (!removeAndSwap("Delete pattern", [p](adaptive::Song& s, adaptive::Renumbering& r) { return s.removePattern(p, &r); })) {
        return false;
    }
    eraseAt(info_.patterns, p);
    return true;
}

bool AudioPlayer::deleteSection(int section) {
    if (section < 0 || section >= static_cast<int>(edit_->numSections)) return false;
    const auto i = static_cast<uint32_t>(section);
    if (!removeAndSwap("Delete section", [i](adaptive::Song& s, adaptive::Renumbering& r) { return s.removeSection(i, &r); })) {
        return false;
    }
    eraseAt(info_.sections, i);
    return true;
}

bool AudioPlayer::deleteChannel(int channel) {
    if (channel < 0 || channel >= static_cast<int>(edit_->numChannels)) return false;
    const auto ch = static_cast<uint32_t>(channel);
    // Its patterns' labels go with them (highest first, as removeChannel takes them).
    std::vector<uint32_t> gone;
    for (uint32_t p = edit_->numPatterns; p-- > 0;) {
        if (edit_->patterns[p].channel == ch) gone.push_back(p);
    }
    if (!removeAndSwap("Delete channel", [ch](adaptive::Song& s, adaptive::Renumbering& r) { return s.removeChannel(ch, &r); })) {
        return false;
    }
    for (uint32_t p : gone) eraseAt(info_.patterns, p);
    eraseAt(info_.channels, ch);
    return true;
}

// ---- Mixer strips ----

bool AudioPlayer::setChannelVolume(int channel, float volume) {
    if (channel < 0 || channel >= static_cast<int>(edit_->numChannels) || !(volume >= 0.0f && volume <= 1.0f)) {
        return false;
    }
    checkpoint("Channel volume", Merge::Volume, static_cast<uint32_t>(channel));
    if (!engine_->post(adaptive::Command::setChannelVolume(static_cast<uint8_t>(channel), volume))) return false;
    edit_->setChannelVolume(channel, volume);
    ++edits_;
    return true;
}

bool AudioPlayer::setChannelMute(int channel, bool on) {
    if (channel < 0 || channel >= static_cast<int>(adaptive::config::kMaxChannels)) return false;
    return engine_->post(adaptive::Command::setChannelMute(static_cast<uint8_t>(channel), on));
}

bool AudioPlayer::setChannelSolo(int channel, bool on) {
    if (channel < 0 || channel >= static_cast<int>(adaptive::config::kMaxChannels)) return false;
    return engine_->post(adaptive::Command::setChannelSolo(static_cast<uint8_t>(channel), on));
}

bool AudioPlayer::setChannelPause(int channel, bool on) {
    if (channel < 0 || channel >= static_cast<int>(adaptive::config::kMaxChannels)) return false;
    return engine_->post(adaptive::Command::setChannelPause(static_cast<uint8_t>(channel), on));
}

bool AudioPlayer::restartChannel(int channel) {
    if (channel < 0 || channel >= static_cast<int>(adaptive::config::kMaxChannels)) return false;
    return engine_->post(adaptive::Command::restartChannel(static_cast<uint8_t>(channel)));
}

bool AudioPlayer::rewind(bool toPattern) {
    return engine_->post(adaptive::Command::rewind(toPattern ? adaptive::RewindTo::Pattern : adaptive::RewindTo::Bar));
}

bool AudioPlayer::setSpeed(float speed, int rampMs) {
    if (!(speed > 0.0f) || rampMs < 0 || rampMs > 60000) return false;
    return engine_->post(adaptive::Command::setSpeed(speed, static_cast<uint16_t>(rampMs)));
}

std::vector<int32_t> AudioPlayer::mixerState() const {
    std::vector<int32_t> out;
    for (uint32_t c = 0; c < edit_->numChannels; ++c) {
        out.push_back(stripFlags_[c].load(std::memory_order_relaxed));
        out.push_back(static_cast<int32_t>(edit_->channels[c].volume * 1000.0f + 0.5f));
    }
    return out;
}
