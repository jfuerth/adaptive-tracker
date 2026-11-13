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
} // namespace

#define JNI_FN(name) Java_io_github_jfuerth_adaptivetracker_NativeAudio_##name

extern "C" {

JNIEXPORT jboolean JNICALL JNI_FN(open)(JNIEnv*, jobject) {
    return player().open() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL JNI_FN(close)(JNIEnv*, jobject) {
    if (gPlayer) gPlayer->close();
}

JNIEXPORT void JNICALL JNI_FN(setPlaying)(JNIEnv*, jobject, jboolean playing) {
    player().setPlaying(playing == JNI_TRUE);
}

JNIEXPORT void JNICALL JNI_FN(setKey)(JNIEnv*, jobject, jint root, jint scaleIndex) {
    player().setKey(root, scaleIndex);
}

JNIEXPORT void JNICALL JNI_FN(setTempo)(JNIEnv*, jobject, jfloat bpm) {
    player().setTempoBpm(bpm);
}

} // extern "C"
