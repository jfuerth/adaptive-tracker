#include "audio_player.hpp"

#include <adaptive/inspect.hpp>

#include <jni.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace {
// One player for the app process. Created on first use from the UI thread.
std::unique_ptr<AudioPlayer> gPlayer;

AudioPlayer& player() {
    if (!gPlayer) gPlayer = std::make_unique<AudioPlayer>();
    return *gPlayer;
}

jboolean toJ(bool b) { return b ? JNI_TRUE : JNI_FALSE; }

// Strings cross as UTF-8 bytes: JNI's own "UTF" functions use modified UTF-8,
// which garbles characters outside the BMP (emoji in a name, say).
jstring toJ(JNIEnv* env, const std::string& s) {
    jbyteArray bytes = env->NewByteArray(static_cast<jsize>(s.size()));
    if (bytes == nullptr) return nullptr;
    env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(s.size()), reinterpret_cast<const jbyte*>(s.data()));
    jclass stringClass = env->FindClass("java/lang/String");
    jmethodID ctor = env->GetMethodID(stringClass, "<init>", "([BLjava/lang/String;)V");
    jstring charset = env->NewStringUTF("UTF-8");
    auto result = static_cast<jstring>(env->NewObject(stringClass, ctor, bytes, charset));
    env->DeleteLocalRef(charset);
    env->DeleteLocalRef(stringClass);
    env->DeleteLocalRef(bytes);
    return result;
}

std::string fromJ(JNIEnv* env, jbyteArray utf8) {
    if (utf8 == nullptr) return {};
    std::string s(static_cast<size_t>(env->GetArrayLength(utf8)), '\0');
    env->GetByteArrayRegion(utf8, 0, static_cast<jsize>(s.size()), reinterpret_cast<jbyte*>(s.data()));
    return s;
}
} // namespace

#define JNI_FN(name) Java_io_github_jfuerth_adaptivetracker_NativeAudio_##name

extern "C" {

JNIEXPORT jboolean JNICALL JNI_FN(open)(JNIEnv*, jobject) { return toJ(player().open()); }

JNIEXPORT void JNICALL JNI_FN(close)(JNIEnv*, jobject) {
    if (gPlayer) gPlayer->close();
}

JNIEXPORT jboolean JNICALL JNI_FN(play)(JNIEnv*, jobject, jboolean hearAll) {
    return toJ(player().play(hearAll == JNI_TRUE));
}

JNIEXPORT jboolean JNICALL JNI_FN(setHearAll)(JNIEnv*, jobject, jboolean on) { return toJ(player().setHearAll(on == JNI_TRUE)); }

JNIEXPORT jboolean JNICALL JNI_FN(stop)(JNIEnv*, jobject) { return toJ(player().stop()); }

JNIEXPORT jboolean JNICALL JNI_FN(gotoSection)(JNIEnv*, jobject, jint section, jint quantize, jint entry,
                                               jboolean viaBridge) {
    return toJ(player().gotoSection(section, quantize, entry, viaBridge == JNI_TRUE));
}

JNIEXPORT jboolean JNICALL JNI_FN(fireEvent)(JNIEnv*, jobject, jint event) {
    return toJ(player().fireEvent(event));
}

JNIEXPORT jboolean JNICALL JNI_FN(setLayerMute)(JNIEnv*, jobject, jint layer, jboolean muted) {
    return toJ(player().setLayerMute(layer, muted == JNI_TRUE));
}

JNIEXPORT jboolean JNICALL JNI_FN(setParam)(JNIEnv*, jobject, jint param, jfloat value) {
    return toJ(player().setParam(param, value));
}

JNIEXPORT jboolean JNICALL JNI_FN(setKey)(JNIEnv*, jobject, jint root, jint scale) {
    return toJ(player().setKey(root, scale));
}

/// The masks of the named scales, in NativeAudio.Scale's order.
JNIEXPORT jintArray JNICALL JNI_FN(getScaleMasks)(JNIEnv* env, jobject) {
    int n = 0;
    const uint16_t* masks = AudioPlayer::scaleMasks(&n);
    std::vector<jint> values(masks, masks + n);
    jintArray result = env->NewIntArray(n);
    if (result != nullptr) env->SetIntArrayRegion(result, 0, n, values.data());
    return result;
}

/// [underruns, bufferFrames, burstFrames, maxCallbackUs, callbackBudgetUs, currentSection, playing]
JNIEXPORT jintArray JNICALL JNI_FN(getStats)(JNIEnv* env, jobject) {
    const AudioPlayer::Stats s = player().stats();
    const jint values[7] = {s.underruns, s.bufferFrames, s.burstFrames,
                            s.maxCallbackUs, s.callbackBudgetUs, s.currentSection, s.playing};
    jintArray result = env->NewIntArray(7);
    if (result != nullptr) env->SetIntArrayRegion(result, 0, 7, values);
    return result;
}

JNIEXPORT jboolean JNICALL JNI_FN(soloPattern)(JNIEnv*, jobject, jint pattern, jint columnMask, jboolean hearAll) {
    return toJ(player().soloPattern(static_cast<uint32_t>(pattern), columnMask, hearAll == JNI_TRUE));
}

JNIEXPORT jboolean JNICALL JNI_FN(setColumnMask)(JNIEnv*, jobject, jint channel, jint columnMask) {
    return toJ(player().setColumnMask(channel, columnMask));
}

JNIEXPORT void JNICALL JNI_FN(resetStats)(JNIEnv*, jobject) { player().resetStats(); }

JNIEXPORT jint JNICALL JNI_FN(getPatternCount)(JNIEnv*, jobject) {
    return static_cast<jint>(player().patternCount());
}

JNIEXPORT jstring JNICALL JNI_FN(getPatternName)(JNIEnv* env, jobject, jint i) {
    return toJ(env, player().patternName(static_cast<uint32_t>(i)));
}

/// Replace the song with a song file. Null on success, else why it failed.
JNIEXPORT jstring JNICALL JNI_FN(loadSong)(JNIEnv* env, jobject, jbyteArray json) {
    const std::string error = player().loadSong(fromJ(env, json));
    return error.empty() ? nullptr : toJ(env, error);
}

/// The song as a song file (UTF-8), with every edit so far.
JNIEXPORT jbyteArray JNICALL JNI_FN(saveSong)(JNIEnv* env, jobject) {
    const std::string json = player().saveSong();
    jbyteArray result = env->NewByteArray(static_cast<jsize>(json.size()));
    if (result != nullptr) {
        env->SetByteArrayRegion(result, 0, static_cast<jsize>(json.size()), reinterpret_cast<const jbyte*>(json.data()));
    }
    return result;
}

JNIEXPORT jlong JNICALL JNI_FN(getEditCount)(JNIEnv*, jobject) {
    return static_cast<jlong>(player().editCount());
}

JNIEXPORT jstring JNICALL JNI_FN(getSongTitle)(JNIEnv* env, jobject) {
    return toJ(env, player().songInfo().title);
}

JNIEXPORT void JNICALL JNI_FN(setSongTitle)(JNIEnv* env, jobject, jbyteArray title) {
    player().setTitle(fromJ(env, title));
}

/// Names of one kind of thing in the song: 0 sections, 1 layers, 2 params, 3 events.
JNIEXPORT jobjectArray JNICALL JNI_FN(getSongNames)(JNIEnv* env, jobject, jint kind) {
    const auto& info = player().songInfo();
    const auto& song = player().song();
    const std::vector<adaptive::songfile::Label>* labels = nullptr;
    uint32_t count = 0;
    switch (kind) {
    case 0: labels = &info.sections; count = song.numSections; break;
    case 1: labels = &info.layers; count = song.numLayers; break;
    case 2: labels = &info.params; count = song.numParams; break;
    case 3: labels = &info.events; count = song.numEvents; break;
    case 4: labels = &info.channels; count = song.numChannels; break;
    case 5: labels = &info.patterns; count = song.numPatterns; break;
    default: break;
    }
    jobjectArray result = env->NewObjectArray(static_cast<jsize>(count), env->FindClass("java/lang/String"), nullptr);
    if (result == nullptr) return nullptr;
    for (uint32_t i = 0; i < count; ++i) {
        const std::string name = labels != nullptr && i < labels->size() ? (*labels)[i].name : std::to_string(i + 1);
        jstring s = toJ(env, name);
        env->SetObjectArrayElement(result, static_cast<jsize>(i), s);
        env->DeleteLocalRef(s);
    }
    return result;
}

JNIEXPORT jint JNICALL JNI_FN(addPatternBytes)(JNIEnv* env, jobject, jint channel, jbyteArray name, jint copyOf) {
    return player().addPattern(channel, fromJ(env, name), copyOf);
}

JNIEXPORT jint JNICALL JNI_FN(addChannelBytes)(JNIEnv* env, jobject, jbyteArray name, jint mode, jint instrument) {
    return player().addChannel(fromJ(env, name), mode, instrument);
}

JNIEXPORT jint JNICALL JNI_FN(addSectionBytes)(JNIEnv* env, jobject, jbyteArray name) {
    return player().addSection(fromJ(env, name));
}

JNIEXPORT jboolean JNICALL JNI_FN(renameBytes)(JNIEnv* env, jobject, jint kind, jint index, jbyteArray name) {
    return toJ(index >= 0 && player().rename(kind, static_cast<uint32_t>(index), fromJ(env, name)));
}

JNIEXPORT jboolean JNICALL JNI_FN(editSequence)(JNIEnv*, jobject, jint section, jint channel, jint op, jint pos,
                                                jint pattern) {
    return toJ(player().editSequence(section, channel, op, pos, pattern));
}

JNIEXPORT jboolean JNICALL JNI_FN(setSection)(JNIEnv*, jobject, jint section, jfloat bpm, jint beatsPerBar,
                                              jint bridgeBeats) {
    return toJ(player().setSection(section, bpm, beatsPerBar, bridgeBeats));
}

/// Pattern indexes `channel` plays in `section`, in order; null if no such section or channel.
JNIEXPORT jintArray JNICALL JNI_FN(getSequence)(JNIEnv* env, jobject, jint section, jint channel) {
    const auto& song = player().song();
    if (section < 0 || section >= song.numSections || channel < 0 || channel >= song.numChannels) return nullptr;
    const adaptive::Sequence& s = song.sections[section].sequences[channel];
    jint entries[adaptive::config::kMaxSequenceLength];
    for (uint32_t i = 0; i < s.length; ++i) entries[i] = s.entries[i];
    jintArray result = env->NewIntArray(s.length);
    if (result != nullptr) env->SetIntArrayRegion(result, 0, s.length, entries);
    return result;
}

/// [tempo in milli-BPM (0: keeps the tempo), beats per bar, bridge beats], or null.
JNIEXPORT jintArray JNICALL JNI_FN(getSectionTiming)(JNIEnv* env, jobject, jint section) {
    const auto& song = player().song();
    if (section < 0 || section >= song.numSections) return nullptr;
    const adaptive::Section& s = song.sections[section];
    const jint values[3] = {static_cast<jint>(s.tempoMilliBpm), s.beatsPerBar, s.bridgeBeats};
    jintArray result = env->NewIntArray(3);
    if (result != nullptr) env->SetIntArrayRegion(result, 0, 3, values);
    return result;
}

/// Per column of the channel: [pitch mode, gate parameter (-1: none), gate threshold x 1000].
JNIEXPORT jintArray JNICALL JNI_FN(getColumnSettings)(JNIEnv* env, jobject, jint channel) {
    const std::vector<int32_t> v = player().columnSettings(channel);
    jintArray result = env->NewIntArray(static_cast<jsize>(v.size()));
    if (result != nullptr) env->SetIntArrayRegion(result, 0, static_cast<jsize>(v.size()), v.data());
    return result;
}

JNIEXPORT jintArray JNICALL JNI_FN(getMixerState)(JNIEnv* env, jobject) {
    const std::vector<int32_t> v = player().mixerState();
    jintArray result = env->NewIntArray(static_cast<jsize>(v.size()));
    if (result != nullptr) env->SetIntArrayRegion(result, 0, static_cast<jsize>(v.size()), v.data());
    return result;
}

JNIEXPORT jboolean JNICALL JNI_FN(setColumnPitch)(JNIEnv*, jobject, jint channel, jint column, jint mode) {
    return toJ(player().setColumnPitch(channel, column, mode));
}

JNIEXPORT jboolean JNICALL JNI_FN(setColumnGate)(JNIEnv*, jobject, jint channel, jint column, jint param,
                                                 jfloat threshold) {
    return toJ(player().setColumnGate(channel, column, param, threshold));
}

JNIEXPORT jint JNICALL JNI_FN(addLayerBytes)(JNIEnv* env, jobject, jbyteArray name) {
    return player().addLayer(fromJ(env, name));
}

JNIEXPORT jboolean JNICALL JNI_FN(removeLayer)(JNIEnv*, jobject, jint layer) {
    return toJ(player().removeLayer(layer));
}

JNIEXPORT jboolean JNICALL JNI_FN(setChannelVolume)(JNIEnv*, jobject, jint ch, jfloat v) {
    return toJ(player().setChannelVolume(ch, v));
}
JNIEXPORT jboolean JNICALL JNI_FN(setChannelMute)(JNIEnv*, jobject, jint ch, jboolean on) {
    return toJ(player().setChannelMute(ch, on == JNI_TRUE));
}
JNIEXPORT jboolean JNICALL JNI_FN(setChannelSolo)(JNIEnv*, jobject, jint ch, jboolean on) {
    return toJ(player().setChannelSolo(ch, on == JNI_TRUE));
}
JNIEXPORT jboolean JNICALL JNI_FN(setChannelPause)(JNIEnv*, jobject, jint ch, jboolean on) {
    return toJ(player().setChannelPause(ch, on == JNI_TRUE));
}
JNIEXPORT jboolean JNICALL JNI_FN(restartChannel)(JNIEnv*, jobject, jint ch) { return toJ(player().restartChannel(ch)); }
JNIEXPORT jboolean JNICALL JNI_FN(rewind)(JNIEnv*, jobject, jboolean toPattern) { return toJ(player().rewind(toPattern)); }
JNIEXPORT jboolean JNICALL JNI_FN(deletePattern)(JNIEnv*, jobject, jint i) { return toJ(player().deletePattern(i)); }
JNIEXPORT jboolean JNICALL JNI_FN(deleteChannel)(JNIEnv*, jobject, jint i) { return toJ(player().deleteChannel(i)); }
JNIEXPORT jboolean JNICALL JNI_FN(deleteSection)(JNIEnv*, jobject, jint i) { return toJ(player().deleteSection(i)); }
JNIEXPORT jboolean JNICALL JNI_FN(undo)(JNIEnv*, jobject) { return toJ(player().undo()); }
JNIEXPORT jboolean JNICALL JNI_FN(redo)(JNIEnv*, jobject) { return toJ(player().redo()); }
JNIEXPORT jstring JNICALL JNI_FN(getUndoLabel)(JNIEnv* env, jobject) { return toJ(env, player().undoLabel()); }
JNIEXPORT jstring JNICALL JNI_FN(getRedoLabel)(JNIEnv* env, jobject) { return toJ(env, player().redoLabel()); }
JNIEXPORT void JNICALL JNI_FN(beginStepBytes)(JNIEnv* env, jobject, jbyteArray label) {
    player().beginStep(fromJ(env, label));
}
JNIEXPORT void JNICALL JNI_FN(endStep)(JNIEnv*, jobject) { player().endStep(); }

JNIEXPORT jboolean JNICALL JNI_FN(setLayerColumns)(JNIEnv*, jobject, jint layer, jint channel, jint columnBits) {
    return toJ(player().setLayerColumns(layer, channel, columnBits));
}

/// Each layer's columns: bit (channel * 4 + column).
JNIEXPORT jintArray JNICALL JNI_FN(getLayerMasks)(JNIEnv* env, jobject) {
    const auto& song = player().song();
    jint masks[adaptive::config::kMaxLayers];
    for (uint32_t i = 0; i < song.numLayers; ++i) masks[i] = static_cast<jint>(song.layers[i]);
    jintArray result = env->NewIntArray(song.numLayers);
    if (result != nullptr) env->SetIntArrayRegion(result, 0, song.numLayers, masks);
    return result;
}

JNIEXPORT jint JNICALL JNI_FN(getInstrumentPitch)(JNIEnv*, jobject, jint instrument) {
    return player().instrumentPitch(instrument);
}

JNIEXPORT jboolean JNICALL JNI_FN(setInstrumentPitch)(JNIEnv*, jobject, jint instrument, jint mode) {
    return toJ(player().setInstrumentPitch(instrument, mode));
}

/// For each section, its bridge length in beats (0: not a bridge).
JNIEXPORT jintArray JNICALL JNI_FN(getSectionBridgeBeats)(JNIEnv* env, jobject) {
    const auto& song = player().song();
    jint beats[adaptive::config::kMaxSections];
    for (uint32_t i = 0; i < song.numSections; ++i) beats[i] = song.sections[i].bridgeBeats;
    jintArray result = env->NewIntArray(song.numSections);
    if (result != nullptr) env->SetIntArrayRegion(result, 0, song.numSections, beats);
    return result;
}

JNIEXPORT jfloatArray JNICALL JNI_FN(getParamInitialValues)(JNIEnv* env, jobject) {
    const auto& song = player().song();
    jfloatArray result = env->NewFloatArray(song.numParams);
    if (result != nullptr) env->SetFloatArrayRegion(result, 0, song.numParams, song.paramInitial);
    return result;
}

/// Effect ticks per beat: a pattern's speed (ticks per row) is counted in these.
JNIEXPORT jint JNICALL JNI_FN(getTicksPerBeat)(JNIEnv*, jobject) {
    return static_cast<jint>(adaptive::config::kEffectTicksPerBeat);
}

/// [channel, numRows, ticksPerRow, numColumns, pitchMode], or null.
JNIEXPORT jintArray JNICALL JNI_FN(getPatternInfo)(JNIEnv* env, jobject, jint i) {
    int32_t info[5];
    if (!player().patternInfo(static_cast<uint32_t>(i), info)) return nullptr;
    jintArray result = env->NewIntArray(5);
    if (result != nullptr) env->SetIntArrayRegion(result, 0, 5, info);
    return result;
}

/// numRows * numColumns * 5 bytes: note, accidental, volume, effect, param; or null.
JNIEXPORT jbyteArray JNICALL JNI_FN(readPattern)(JNIEnv* env, jobject, jint i) {
    int32_t info[5];
    if (!player().patternInfo(static_cast<uint32_t>(i), info)) return nullptr;
    const uint32_t size = static_cast<uint32_t>(info[1] * info[3] * 5);
    std::unique_ptr<uint8_t[]> bytes(new uint8_t[size]);
    if (!player().readPattern(static_cast<uint32_t>(i), bytes.get(), size)) return nullptr;
    jbyteArray result = env->NewByteArray(static_cast<jsize>(size));
    if (result != nullptr) {
        env->SetByteArrayRegion(result, 0, static_cast<jsize>(size), reinterpret_cast<const jbyte*>(bytes.get()));
    }
    return result;
}

JNIEXPORT jboolean JNICALL JNI_FN(setCell)(JNIEnv*, jobject, jint pattern, jint row, jint column, jint note,
                                           jint accidental, jint volume, jint effect, jint param) {
    adaptive::Cell cell;
    cell.note = static_cast<uint8_t>(note);
    cell.accidental = static_cast<int8_t>(accidental);
    cell.volume = static_cast<uint8_t>(volume);
    cell.effect = static_cast<uint8_t>(effect);
    cell.effectParam = static_cast<uint8_t>(param);
    return toJ(player().setCell(static_cast<uint32_t>(pattern), row, column, cell));
}

JNIEXPORT jboolean JNICALL JNI_FN(previewOn)(JNIEnv*, jobject, jint instrument, jint slot, jint midiNote) {
    return toJ(player().previewOn(instrument, slot, midiNote));
}

JNIEXPORT jint JNICALL JNI_FN(getChannelCount)(JNIEnv*, jobject) {
    return static_cast<jint>(player().channelCount());
}

JNIEXPORT jstring JNICALL JNI_FN(getChannelName)(JNIEnv* env, jobject, jint i) {
    return toJ(env, player().channelName(static_cast<uint32_t>(i)));
}

JNIEXPORT jint JNICALL JNI_FN(getChannelColumns)(JNIEnv*, jobject, jint i) {
    return player().channelColumns(i);
}

JNIEXPORT jint JNICALL JNI_FN(getInstrumentCount)(JNIEnv*, jobject) {
    return static_cast<jint>(player().instrumentCount());
}

JNIEXPORT jstring JNICALL JNI_FN(getInstrumentName)(JNIEnv* env, jobject, jint i) {
    return toJ(env, player().instrumentName(static_cast<uint32_t>(i)));
}

JNIEXPORT jboolean JNICALL JNI_FN(renameInstrument)(JNIEnv* env, jobject, jint i, jbyteArray name) {
    return toJ(player().renameInstrument(static_cast<uint32_t>(i), fromJ(env, name)));
}

/// Instrument::kNumParams values, or null.
JNIEXPORT jfloatArray JNICALL JNI_FN(readInstrument)(JNIEnv* env, jobject, jint i) {
    float values[adaptive::Instrument::kNumParams];
    if (!player().readInstrument(static_cast<uint32_t>(i), values, adaptive::Instrument::kNumParams)) return nullptr;
    jfloatArray result = env->NewFloatArray(adaptive::Instrument::kNumParams);
    if (result != nullptr) env->SetFloatArrayRegion(result, 0, adaptive::Instrument::kNumParams, values);
    return result;
}

JNIEXPORT jboolean JNICALL JNI_FN(setPatternShape)(JNIEnv*, jobject, jint pattern, jint rows, jint ticksPerRow) {
    return toJ(pattern >= 0 && player().setPatternShape(static_cast<uint32_t>(pattern), rows, ticksPerRow));
}

JNIEXPORT jboolean JNICALL JNI_FN(clearCells)(JNIEnv*, jobject, jint pattern, jint column, jint first, jint last,
                                               jint parts) {
    return toJ(pattern >= 0 && player().clearCells(static_cast<uint32_t>(pattern), column, first, last, parts));
}

JNIEXPORT jboolean JNICALL JNI_FN(scaleRows)(JNIEnv*, jobject, jint pattern, jint num, jint den) {
    return toJ(pattern >= 0 && player().scaleRows(static_cast<uint32_t>(pattern), num, den));
}

JNIEXPORT jboolean JNICALL JNI_FN(setColumns)(JNIEnv*, jobject, jint channel, jint count) {
    return toJ(player().setColumns(channel, count));
}

JNIEXPORT jboolean JNICALL JNI_FN(removeColumn)(JNIEnv*, jobject, jint channel, jint column) {
    return toJ(player().removeColumn(channel, column));
}

JNIEXPORT jint JNICALL JNI_FN(getMaxInstruments)(JNIEnv*, jobject) {
    return static_cast<jint>(player().maxInstruments());
}

/// New instrument called `name` with `values` (null: defaults). Its index, or -1.
JNIEXPORT jint JNICALL JNI_FN(addInstrument)(JNIEnv* env, jobject, jfloatArray values, jbyteArray name) {
    const std::string n = fromJ(env, name);
    if (values == nullptr) return player().addInstrument(nullptr, n);
    if (env->GetArrayLength(values) != adaptive::Instrument::kNumParams) return -1;
    float v[adaptive::Instrument::kNumParams];
    env->GetFloatArrayRegion(values, 0, adaptive::Instrument::kNumParams, v);
    return player().addInstrument(v, n);
}

JNIEXPORT jboolean JNICALL JNI_FN(assignInstrument)(JNIEnv*, jobject, jint channel, jint column, jint instrument) {
    return toJ(player().assignInstrument(channel, column, instrument));
}

JNIEXPORT jboolean JNICALL JNI_FN(setMonitor)(JNIEnv*, jobject, jint instrument) {
    return toJ(player().setMonitor(instrument));
}

/// The latest `n` samples of the monitored instrument, oldest first.
JNIEXPORT jfloatArray JNICALL JNI_FN(readMonitor)(JNIEnv* env, jobject, jint n) {
    if (n <= 0 || n > static_cast<jint>(AudioPlayer::kMonitorFrames)) return nullptr;
    std::unique_ptr<float[]> samples(new float[static_cast<size_t>(n)]);
    player().readMonitor(samples.get(), static_cast<uint32_t>(n));
    jfloatArray result = env->NewFloatArray(n);
    if (result != nullptr) env->SetFloatArrayRegion(result, 0, n, samples.get());
    return result;
}

JNIEXPORT jboolean JNICALL JNI_FN(setInstrumentParam)(JNIEnv*, jobject, jint i, jint param, jfloat value) {
    return toJ(player().setInstrumentParam(static_cast<uint32_t>(i), param, value));
}

// ---- Read from pressense and the engine, so the UI doesn't duplicate them ----

/// Each instrument parameter's stable key, in the engine's order.
JNIEXPORT jobjectArray JNICALL JNI_FN(getInstrumentParamKeys)(JNIEnv* env, jobject) {
    using adaptive::Instrument;
    jobjectArray result =
        env->NewObjectArray(Instrument::kNumParams, env->FindClass("java/lang/String"), nullptr);
    if (result == nullptr) return nullptr;
    for (uint8_t i = 0; i < Instrument::kNumParams; ++i) {
        jstring key = env->NewStringUTF(Instrument::key(static_cast<Instrument::Param>(i)));
        env->SetObjectArrayElement(result, i, key);
        env->DeleteLocalRef(key);
    }
    return result;
}

/// [min, max] per parameter, in the engine's order.
JNIEXPORT jfloatArray JNICALL JNI_FN(getInstrumentParamRanges)(JNIEnv* env, jobject) {
    using adaptive::Instrument;
    float ranges[Instrument::kNumParams * 2];
    for (uint8_t i = 0; i < Instrument::kNumParams; ++i) {
        const auto r = Instrument::range(static_cast<Instrument::Param>(i));
        ranges[i * 2] = r.min;
        ranges[i * 2 + 1] = r.max;
    }
    jfloatArray result = env->NewFloatArray(Instrument::kNumParams * 2);
    if (result != nullptr) env->SetFloatArrayRegion(result, 0, Instrument::kNumParams * 2, ranges);
    return result;
}

/// `n` samples of pressense's oscillator at `shape`, spanning `cycles` cycles.
JNIEXPORT jfloatArray JNICALL JNI_FN(renderWaveform)(JNIEnv* env, jobject, jfloat shape, jfloat cycles, jint n) {
    if (n <= 0 || n > 4096) return nullptr;
    std::unique_ptr<float[]> samples(new float[static_cast<size_t>(n)]);
    adaptive::inspect::renderWaveform(shape, cycles, samples.get(), static_cast<uint32_t>(n));
    jfloatArray result = env->NewFloatArray(n);
    if (result != nullptr) env->SetFloatArrayRegion(result, 0, n, samples.get());
    return result;
}

/// pressense's envelope for a held-then-released note:
/// [attack s, decay s, release s, releaseUnfinished (0/1), then 32 levels each
/// for attack, decay and release].
JNIEXPORT jfloatArray JNICALL JNI_FN(traceEnvelope)(JNIEnv* env, jobject, jfloat a, jfloat d, jfloat s, jfloat r) {
    using adaptive::inspect::EnvelopeTrace;
    const EnvelopeTrace t = adaptive::inspect::traceEnvelope(a, d, s, r);
    constexpr jsize kSize = 4 + EnvelopeTrace::kStages * EnvelopeTrace::kPoints;
    float out[kSize];
    for (uint32_t i = 0; i < EnvelopeTrace::kStages; ++i) out[i] = t.seconds[i];
    out[3] = t.releaseUnfinished ? 1.0f : 0.0f;
    for (uint32_t i = 0; i < EnvelopeTrace::kStages; ++i) {
        for (uint32_t k = 0; k < EnvelopeTrace::kPoints; ++k) out[4 + i * EnvelopeTrace::kPoints + k] = t.levels[i][k];
    }
    jfloatArray result = env->NewFloatArray(kSize);
    if (result != nullptr) env->SetFloatArrayRegion(result, 0, kSize, out);
    return result;
}

/// What an effect's parameter means, in natural units: Hz for F, Q for 5,
/// shape 0..1 for 6. NaN for other effects.
JNIEXPORT jfloat JNICALL JNI_FN(effectValue)(JNIEnv*, jobject, jint effect, jint param) {
    namespace fxmap = adaptive::fxmap;
    const auto p = static_cast<uint8_t>(param);
    switch (effect) {
    case adaptive::fx::kFilterCutoff: return fxmap::cutoffHz(p);
    case adaptive::fx::kFilterResonance: return fxmap::resonanceQ(p);
    case adaptive::fx::kWaveShape: return fxmap::waveShape(p);
    default: return NAN;
    }
}

JNIEXPORT jint JNICALL JNI_FN(getColumnInstrument)(JNIEnv*, jobject, jint channel, jint column) {
    return player().columnInstrument(channel, column);
}

JNIEXPORT jboolean JNICALL JNI_FN(previewOff)(JNIEnv*, jobject, jint slot) {
    return toJ(player().previewOff(slot));
}

/// [patternIndex or -1, row or -1]
JNIEXPORT jintArray JNICALL JNI_FN(getPlayPosition)(JNIEnv* env, jobject, jint channel) {
    int32_t pos[2];
    player().playPosition(channel, pos);
    jintArray result = env->NewIntArray(2);
    if (result != nullptr) env->SetIntArrayRegion(result, 0, 2, pos);
    return result;
}

/// [root, scaleMask] of the key in effect now.
JNIEXPORT jintArray JNICALL JNI_FN(getCurrentKey)(JNIEnv* env, jobject) {
    int32_t key[2];
    player().currentKey(key);
    jintArray result = env->NewIntArray(2);
    if (result != nullptr) env->SetIntArrayRegion(result, 0, 2, key);
    return result;
}

/// [root, scaleMask] of the song's home key.
JNIEXPORT jintArray JNICALL JNI_FN(getHomeKey)(JNIEnv* env, jobject) {
    int32_t key[2];
    player().homeKey(key);
    jintArray result = env->NewIntArray(2);
    if (result != nullptr) env->SetIntArrayRegion(result, 0, 2, key);
    return result;
}

} // extern "C"
