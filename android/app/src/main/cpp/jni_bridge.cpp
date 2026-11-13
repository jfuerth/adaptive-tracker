#include "audio_player.hpp"

#include <jni.h>

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

/// [underruns, bufferFrames, burstFrames, maxCallbackUs, callbackBudgetUs, currentSection]
JNIEXPORT jintArray JNICALL JNI_FN(getStats)(JNIEnv* env, jobject) {
    const AudioPlayer::Stats s = player().stats();
    const jint values[6] = {s.underruns, s.bufferFrames, s.burstFrames,
                            s.maxCallbackUs, s.callbackBudgetUs, s.currentSection};
    jintArray result = env->NewIntArray(6);
    if (result != nullptr) env->SetIntArrayRegion(result, 0, 6, values);
    return result;
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

JNIEXPORT jboolean JNICALL JNI_FN(previewOn)(JNIEnv*, jobject, jint channel, jint slot, jint midiNote) {
    return toJ(player().previewOn(channel, slot, midiNote));
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
