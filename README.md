# Adaptive Tracker

An Android app (in progress) for composing adaptive, game-style music, combining
a tracker-style sequencer, the [pressense](https://github.com/jfuerth/pressense)
synth engine and Lua scripting. The playback core is portable C++ intended to
run on microcontrollers (ESP32, RP2350) as well.

See [DESIGN.md](DESIGN.md) for the design, terminology and roadmap.

## Layout

- `core/engine/include/adaptive/`: portable, header-only playback engine (no heap
  after setup, builds with `-fno-exceptions -fno-rtti`).
- `core/tests/`: host unit tests (doctest) and a portability compile check.
- `core/third_party/`: pressense (git submodule) and doctest.
- `core/demo/`: demo songs shared by the desktop tools and the app.
- `android/`: the Android app (Kotlin + Compose UI, C++ engine via JNI, Oboe audio).
- `desktop/`: host-side tools: `render_demo` (renders a demo song to WAV) and
  `key_experiment` (renders one arrangement in several keys and scales).

## Building on the desktop

```sh
git submodule update --init core/third_party/pressense
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
./build/adaptive_tests
./build/render_demo build/demo.wav 16    # then play build/demo.wav
```

Requires CMake 3.22 or newer and a C++20 compiler.

## Building the Android app

Needs the Android SDK with NDK 30.0.16248370 and CMake 4.1.2 (install with
`sdkmanager "ndk;30.0.16248370" "cmake;4.1.2" platform-tools`), and JDK 17 or newer.
`android/local.properties` points Gradle at the SDK (`sdk.dir=...`).

```sh
cd android
./gradlew assembleRelease     # app/build/outputs/apk/release/app-release.apk
./gradlew installRelease      # install on a connected device
```

The release build is signed with the debug key so it can be sideloaded. Use a
real keystore before distributing it anywhere.
