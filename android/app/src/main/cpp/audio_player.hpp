#pragma once

#include <adaptive/engine.hpp>
#include <adaptive/songfile.hpp>

#include <oboe/Oboe.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

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

    /// Replace the song with a song file's contents (playback stops and
    /// restarts with a fresh engine). Returns "" or why it failed, in which
    /// case the old song stays. UI thread only.
    std::string loadSong(std::string_view json);
    /// The song as a song file, with every edit made so far. UI thread only.
    std::string saveSong() const;
    const adaptive::songfile::SongInfo& songInfo() const { return info_; }
    /// The UI thread's copy of the song (see edit_).
    const adaptive::Song& song() const { return *edit_; }
    void setTitle(std::string title) { info_.title = std::move(title); ++edits_; }
    /// Counts edits to the song, for autosave: unchanged count, nothing to save.
    uint64_t editCount() const { return edits_; }

    // Control (UI thread only). Each returns false if the command queue was full.
    bool play();
    bool stop();
    /// viaBridge: play the song's first bridge section (if any) on the way.
    bool gotoSection(int section, int quantize, int entry, bool viaBridge);
    /// The first section with bridge beats, or -1.
    int firstBridge() const;
    bool fireEvent(int event);
    bool setLayerMute(int layer, bool muted);
    bool setParam(int param, float value);
    /// root: pitch class 0-11; scale: 12-bit mask. Applies at the next bar.
    bool setKey(int root, int scale);
    /// The scales the app offers by name, in NativeAudio.Scale's order.
    static const uint16_t* scaleMasks(int* count);

    // Pattern editing (UI thread only).
    uint32_t patternCount() const { return edit_->numPatterns; }
    std::string patternName(uint32_t i) const { return name(info_.patterns, i); }
    /// [channel, numRows, ticksPerRow, numColumns, pitchMode (0 Absolute, 1 Transpose, 2 Degree)]
    bool patternInfo(uint32_t i, int32_t out[5]);
    /// Copies the pattern's cells, 5 bytes each (note, accidental, volume, effect,
    /// param), row by row, numColumns per row.
    bool readPattern(uint32_t i, uint8_t* out, uint32_t capacity);
    bool setCell(uint32_t pattern, int row, int column, const adaptive::Cell& cell);
    // Shape edits; see the Song methods of the same names.
    bool setPatternShape(uint32_t pattern, int rows, int ticksPerRow);
    bool clearCells(uint32_t pattern, int column, int first, int last, int parts);
    bool scaleRows(uint32_t pattern, int num, int den);
    bool setColumns(int channel, int count);
    bool removeColumn(int channel, int column);
    /// Sound `midiNote` with song instrument `instrument` on preview voice `slot`.
    bool previewOn(int instrument, int slot, int midiNote);
    bool previewOff(int slot);
    /// Loop pattern-table entry `pattern` alone; `columnMask` picks the columns heard.
    bool soloPattern(uint32_t pattern, int columnMask);
    bool setColumnMask(int channel, int columnMask);

    // Growing the song (UI thread only). Each returns the new index, or -1 when full.
    /// A pattern for `channel`: empty, or a copy of pattern `copyOf` (>= 0).
    int addPattern(int channel, const std::string& name, int copyOf);
    /// A channel with one column playing `instrument` (-1: none), in pitch mode `mode`.
    int addChannel(const std::string& name, int mode, int instrument);
    int addSection(const std::string& name);
    /// kind: 0 sections, 1 layers, 2 params, 3 events, 4 channels, 5 patterns. Ids stay.
    bool rename(int kind, uint32_t index, const std::string& name);
    /// op: adaptive::SeqEdit. See Song::editSequence.
    bool editSequence(int section, int channel, int op, int pos, int pattern);
    /// Section timing; bpm 0 keeps the tempo. See Song::setSectionTiming.
    bool setSection(int section, float bpm, int beatsPerBar, int bridgeBeats);

    // Column settings and layers (UI thread only).
    /// Per column of `channel` (numColumns of them): pitch mode, gate parameter (-1: none), gate threshold x 1000.
    std::vector<int32_t> columnSettings(int channel) const;
    /// Column's pitch mode; its notes are converted. See Song::setColumnPitch.
    bool setColumnPitch(int channel, int column, int mode);
    /// param -1: the column always plays.
    bool setColumnGate(int channel, int column, int param, float threshold);
    int addLayer(const std::string& name);
    bool removeLayer(int layer);
    bool setLayerColumns(int layer, int channel, int columnBits);
    /// Instrument's pitch-mode hint (-1: none): what a column switches to when given it. Editing data only.
    int instrumentPitch(int instrument) const;
    bool setInstrumentPitch(int instrument, int mode);

    // Instruments (UI thread only).
    uint32_t channelCount() const { return edit_->numChannels; }
    std::string channelName(uint32_t i) const { return name(info_.channels, i); }
    /// Columns (voices) on channel i.
    int channelColumns(int i) const {
        return i >= 0 && i < static_cast<int>(channelCount())
                   ? static_cast<int>(edit_->channels[i].numColumns) : 0;
    }
    uint32_t instrumentCount() const { return edit_->numInstruments; }
    uint32_t maxInstruments() const { return adaptive::config::kMaxInstruments; }
    std::string instrumentName(uint32_t i) const { return name(info_.instruments, i); }
    /// Add an instrument called `name` with `values` (Instrument::kNumParams
    /// floats), or the defaults when null. Returns its index, or -1 when the song is full.
    int addInstrument(const float* values, const std::string& name);
    /// Renames instrument i; its id (what the song file refers to it by) stays.
    bool renameInstrument(uint32_t i, const std::string& name);
    /// Copies instrument i's parameter values (Instrument::kNumParams floats).
    bool readInstrument(uint32_t i, float* out, uint32_t capacity) const;
    bool setInstrumentParam(uint32_t i, int param, float value);
    /// Instrument index used by a channel's column, or -1 (no instrument, or no such column).
    int columnInstrument(int channel, int column) const;
    /// Channel's column plays instrument `instrument` from now on.
    bool assignInstrument(int channel, int column, int instrument);
    /// Which instrument the monitor output carries (-1: none).
    bool setMonitor(int instrument);
    /// Copies the latest `n` monitor samples (oldest first) into `out`; n <= kMonitorFrames.
    void readMonitor(float* out, uint32_t n) const;
    static constexpr uint32_t kMonitorFrames = 8192;

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
    static std::string name(const std::vector<adaptive::songfile::Label>& labels, uint32_t i) {
        return i < labels.size() ? labels[i].name : std::string();
    }
    void resetPositions();
    bool openLocked();  // streamMutex_ must be held
    void updateStats(oboe::AudioStream* stream, int64_t callbackNs, int32_t numFrames);

    std::unique_ptr<adaptive::Engine> engine_;
    /// The song the engine plays. Once audio runs, only the audio thread
    /// touches it (edits reach it as commands).
    std::unique_ptr<adaptive::Song> song_;
    /// The UI thread's copy of the song: every edit is made here as well as
    /// sent to the engine, so the UI can read it, and save it, without racing
    /// the audio thread.
    std::unique_ptr<adaptive::Song> edit_;
    adaptive::songfile::SongInfo info_;
    uint64_t edits_ = 0;
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

    // The monitored instrument's output: a ring the audio thread writes and
    // the UI reads. A read can overlap a write at its oldest end; that's fine
    // for drawing.
    static constexpr uint32_t kMonitorScratch = 1024;
    float monitorScratch_[kMonitorScratch] = {};
    float monitorRing_[kMonitorFrames] = {};
    std::atomic<uint32_t> monitorWrite_{0};  // total frames written, wraps
};
