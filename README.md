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
- `desktop/`: host-side tools, such as `render_demo` (renders a demo song to WAV).

## Building on the desktop

```sh
git submodule update --init core/third_party/pressense
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
./build/adaptive_tests
./build/render_demo build/demo.wav 16    # then play build/demo.wav
```

Requires CMake 3.22 or newer and a C++20 compiler.
