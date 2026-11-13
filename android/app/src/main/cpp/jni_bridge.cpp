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

} // extern "C"
