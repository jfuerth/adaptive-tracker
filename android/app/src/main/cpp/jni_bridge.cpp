#include "audio_player.hpp"

#include <adaptive/inspect.hpp>

#include <jni.h>

#include <cmath>
#include <memory>

namespace {
// One player for the app process. Created on first use from the UI thread.
std::unique_ptr<AudioPlayer> gPlayer;

AudioPlayer& player() {
    if (!gPlayer) gPlayer = std::make_unique<AudioPlayer>();
    return *gPlayer;
}

jboolean toJ(bool b) { return b ? JNI_TRUE : JNI_FALSE; }
} // namespace

#define JNI_FN(name) Java_io_github_jfuerth_adaptivetracker_NativeAudio_##name

extern "C" {

JNIEXPORT jboolean JNICALL JNI_FN(open)(JNIEnv*, jobject) { return toJ(player().open()); }

JNIEXPORT void JNICALL JNI_FN(close)(JNIEnv*, jobject) {
    if (gPlayer) gPlayer->close();
}

JNIEXPORT jboolean JNICALL JNI_FN(play)(JNIEnv*, jobject) { return toJ(player().play()); }

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

JNIEXPORT jboolean JNICALL JNI_FN(setKey)(JNIEnv*, jobject, jint root, jint scaleIndex) {
    return toJ(player().setKey(root, scaleIndex));
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

JNIEXPORT jboolean JNICALL JNI_FN(soloPattern)(JNIEnv*, jobject, jint pattern, jint columnMask) {
    return toJ(player().soloPattern(static_cast<uint32_t>(pattern), columnMask));
}

JNIEXPORT jboolean JNICALL JNI_FN(setColumnMask)(JNIEnv*, jobject, jint channel, jint columnMask) {
    return toJ(player().setColumnMask(channel, columnMask));
}

JNIEXPORT void JNICALL JNI_FN(resetStats)(JNIEnv*, jobject) { player().resetStats(); }

JNIEXPORT jint JNICALL JNI_FN(getPatternCount)(JNIEnv*, jobject) {
    return static_cast<jint>(player().patternCount());
}

JNIEXPORT jstring JNICALL JNI_FN(getPatternName)(JNIEnv* env, jobject, jint i) {
    return env->NewStringUTF(player().patternName(static_cast<uint32_t>(i)));
}

/// [channel, numRows, rowsPerBeat, numColumns, pitchMode], or null.
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
    return env->NewStringUTF(player().channelName(static_cast<uint32_t>(i)));
}

JNIEXPORT jint JNICALL JNI_FN(getChannelColumns)(JNIEnv*, jobject, jint i) {
    return player().channelColumns(i);
}

JNIEXPORT jint JNICALL JNI_FN(getInstrumentCount)(JNIEnv*, jobject) {
    return static_cast<jint>(player().instrumentCount());
}

JNIEXPORT jstring JNICALL JNI_FN(getInstrumentName)(JNIEnv* env, jobject, jint i) {
    return env->NewStringUTF(player().instrumentName(static_cast<uint32_t>(i)));
}

/// Instrument::kNumParams values, or null.
JNIEXPORT jfloatArray JNICALL JNI_FN(readInstrument)(JNIEnv* env, jobject, jint i) {
    float values[adaptive::Instrument::kNumParams];
    if (!player().readInstrument(static_cast<uint32_t>(i), values, adaptive::Instrument::kNumParams)) return nullptr;
    jfloatArray result = env->NewFloatArray(adaptive::Instrument::kNumParams);
    if (result != nullptr) env->SetFloatArrayRegion(result, 0, adaptive::Instrument::kNumParams, values);
    return result;
}

JNIEXPORT jint JNICALL JNI_FN(getMaxInstruments)(JNIEnv*, jobject) {
    return static_cast<jint>(player().maxInstruments());
}

/// New instrument with `values` (null: defaults). Its index, or -1.
JNIEXPORT jint JNICALL JNI_FN(addInstrument)(JNIEnv* env, jobject, jfloatArray values) {
    if (values == nullptr) return player().addInstrument(nullptr);
    if (env->GetArrayLength(values) != adaptive::Instrument::kNumParams) return -1;
    float v[adaptive::Instrument::kNumParams];
    env->GetFloatArrayRegion(values, 0, adaptive::Instrument::kNumParams, v);
    return player().addInstrument(v);
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
