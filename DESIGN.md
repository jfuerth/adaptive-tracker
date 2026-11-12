# Adaptive Tracker — Design

An Android app for composing **adaptive music** (game-style music that reacts to
state) by combining a tracker-style sequencer, the Pressense synth engine, and
Lua scripting. Composed on a phone; the playback core must also be able to run on
microcontrollers (ESP32, RP2350) later.

## Decisions so far

| Topic | Decision |
|---|---|
| Synth | Pressense `lib/synth`, synth-only for now (no samples). Repo: `git@github.com:jfuerth/pressense.git`. (The local checkout at `../pressence/pressence` has a misspelled directory name.) |
| Pressense integration | **Git submodule of the whole repo for now**, using `lib/synth`, `lib/features` and `lib/nlohmann`. Move `lib/synth` into its own repo later, once its interface has settled for this project. Optional upstream cleanup: move the JSON conversion out of `performance_timer.hpp` so the synth no longer needs nlohmann |
| Programmatic control | Lua, chosen for realistic overhead on MCUs |
| Export / in-game runtime | Not yet. Phone-only for now, but the core stays portable |
| UI | Kotlin + Jetpack Compose; C++ core behind JNI |
| Adaptive effects (v1) | Channel mute/unmute, channel effects, master effects (filter, reverb), global tempo, instrument parameter changes, key change |
| Mute | Musical mute first: stop new notes, let current notes release, quantised. A click-free "audio mute" fade may come later, e.g. for choppy effects |
| Reverb | Phone only for now. Not required on MCUs |
| Instrument changes | Parameter changes only. A per-channel instrument override table may come later |
| Chords | Several columns per channel, one voice per column. No chord shorthand yet |
| Pattern scope | **Per channel.** Each channel plays its own sequence of patterns, which can have different lengths. A section says which pattern(s) each channel plays |
| Timing | Engine clock in ticks at 96 PPQ (divisible by 2, 3, 4, 8, 16, 32). Each pattern sets its own **length in rows** and **rows per beat**, so channels can play polyrhythms. **Beats per bar is set per section** and defines bar quantisation, counted from section start. Patterns needn't fill whole bars |
| Tempo | Comes only from the section plus adaptive mappings (a multiplier). **Patterns cannot change tempo** |
| Android toolchain | Command-line only, via `sdkmanager`: NDK 30.0.16248370, CMake 4.1.2 and platform-tools in `~/Android/Sdk`. The Gradle wrapper fetches Gradle. Android Studio is optional |
| PKTrekker | Reference only (effect semantics, tests). Not linked. Check licence before copying anything: `Modplay.*` is co-copyright Andrew Kilpatrick |

## Nomenclature

The UI is modelled on a **mixing console**. Tracker terms are kept where they
don't collide with console terms.

| Term | Meaning | Notes on prior usage |
|---|---|---|
| **Song** | The whole project: channels, patterns, instruments, sections, scripts. | Classic trackers call this a *module* (`.mod`). We avoid "module" because it suggests a component. |
| **Channel** | A vertical console strip: gain, pan, mute, filter, effect sends, and so on. Its audio source is the tracker data that plays on it. | In MOD/XM trackers, "channel" means one pattern column. Here it means the mixer strip, like Renoise's *track* or a DAW channel. |
| **Column** | One monophonic lane of notes inside a channel: note, instrument, volume and effect fields per row. Each column drives one pressense `synth::Voice`. A channel with several columns can play chords. | Same idea as Renoise's *note column* and the MOD "channel". |
| **Row** | One time step. | Standard tracker usage. |
| **Pattern** | A block of rows. | Standard. Whether a pattern spans all channels or belongs to one channel is an open question. |
| **Instrument** | A pressense synth patch: oscillator, filter, envelopes, LFO and modulation settings. | Called a *patch* in pressense; a *sample* or *instrument* in trackers. |
| **Channel effects** | DSP on a channel strip (filter, drive, sends, pan) plus offsets to the tracker effect columns. Adaptive control works at the channel level, not on individual columns. | |
| **Master / global effects** | The shared output stage: pressense post-filter, drive and clipping, plus reverb. | |
| **Section** | An adaptive state (explore, combat, and so on) with its own pattern order. | |
| **Layer** | A named group of channels that are muted, unmuted or faded together. | |
| **Game parameter** | A named float set by a UI slider, a script, or (later) the host game. | |
| **Target** | Any addressable adaptive value, e.g. `channel[2].mute`, `channel[2].filter.cutoff`, `master.filter.cutoff`, `tempo`, `instrument["lead"].filter_q`, `key.root`. | |
| **Mapping** | Data that drives a target from a game parameter (curve, range, ramp, quantise). | |

## Layers

```
┌──────────────────────── Android app (Kotlin/Compose) ────────────────────────┐
│ Pattern grid editor · Patch editor · Parameter sliders · Lua script editor    │
└───────────────────────────────┬──────────────────────────────────────────────┘
                                │ JNI (commands in, state snapshots out)
┌───────────────────────────────▼──────────────────────────────────────────────┐
│ core/control  (control thread, MCU core 0)                                    │
│   Lua VM (optional) — runs at tick/row rate, never on the audio thread        │
│   Game parameters (named floats) · rule evaluation                            │
└───────────────────────────────┬──────────────────────────────────────────────┘
                                │ lock-free SPSC command queue (fixed size)
┌───────────────────────────────▼──────────────────────────────────────────────┐
│ core/engine   (audio thread, MCU core 1) — portable C++, no heap, no except.  │
│   Adaptive director: sections, quantised transitions, layer gains             │
│   Sequencer: channel → pattern sequence → rows → columns (96 PPQ clock)       │
│   Column players: one pressense synth::Voice per column                       │
│   Channel strips (gain/pan/filter/sends) → master (filter, reverb, drive)     │
└───────────────────────────────┬──────────────────────────────────────────────┘
                                │ float buffer
             Oboe (Android) · ALSA/JACK (desktop tests) · I2S (MCU, later)
```

### Portability rules for `core/engine`
Follow pressense's CODING_STANDARDS.md: dependency injection over `#ifdef`, and
no heap allocation after setup. In addition:
- Builds with `-fno-exceptions -fno-rtti`.
- Fixed-capacity containers sized by compile-time config (channel count, columns
  per channel, pattern length, pattern pool), so we know the RAM budget for each MCU.
- Single-precision floats only in the hot path.
- `synth::WavetableSynth::nextSample` with `features::NoOpTimingPolicy`.

## Adaptive model (first cut)

- **Song** = set of **Sections** (e.g. explore, tension, combat, victory).
- A **Section** says which pattern sequence each channel plays, its beats per
  bar, and its tempo, and has a set of **Layers**. A layer is a group of channels
  that can be muted or faded together (vertical remixing).
- **Transitions** (horizontal re-sequencing): "go to section X" requests are
  quantised by the engine to the next beat, bar, or pattern end, with an optional
  transition pattern (stinger/bridge). Scripts *request*; the engine executes
  sample-accurately. Timing therefore stays exact even if Lua runs late.
- **Game parameters** are named floats set by UI sliders (on the phone) or by the
  host game (later). Simple mappings (parameter → layer gain, parameter threshold
  → section change) are data and need no Lua.

## Lua

- Lua 5.4, embedded. Allocator is a fixed arena (`lua_newstate` with custom
  alloc) so memory is bounded and deterministic on MCUs.
- Hooks: `on_param(name, value)`, `on_bar(section, bar)`, `on_row(...)`, plus an
  API to set params, request transitions, set layer gains, tweak synth params,
  and write pattern cells (generative bits).
- Runs only on the control thread. Output is commands into the queue.
- Engine works fully without Lua; Lua is an optional component.
- Budget check needed: Lua 5.4 is roughly 100–200 KB flash plus tens of KB of
  heap. Fine for ESP32 and RP2350 (520 KB SRAM); measure early.

## Repo layout (proposed)

```
core/
  engine/        portable sequencer + adaptive director (header-heavy C++20)
  control/       Lua binding, parameter store
  third_party/   pressense (git submodule; include lib/synth, lib/features, lib/nlohmann), lua
  tests/         host unit tests (Catch2 or doctest), offline render to WAV
desktop/         tiny CLI player for host-side listening/debugging
android/         Gradle project: app (Compose) + :native (CMake → core + Oboe)
```

## First milestones

1. ✅ **Core skeleton + host tests** (done 2026-11-12): pressense submodule, one channel playing a
   hard-coded pattern through `WavetableSynth`, offline-rendered to WAV.
2. **Sequencer**: per-channel patterns of mixed lengths and rows per beat, a small
   effect set (volume, slide, arpeggio, synth-param automation), several channels.
   Includes the scale-degree listening experiment.
3. **Android shell**: Gradle + NDK + Oboe; play/stop a demo song from the phone.
4. **Adaptive director**: sections, layers, quantised transitions, parameter sliders.
5. **Pattern editor UI** (touch grid) and patch editor.
6. **Lua**: control thread, API, script editor; measure footprint.
7. Later: song file format, MCU build (RP2350 first, reusing pressense's I2S sink).

## Future enhancement ideas

- **Arrangement editor:** an edit mode that lays out each channel's whole pattern
  sequence for the song side by side. You edit patterns where they appear, and
  every other instance of the same pattern updates live. This gives the
  "everything at once" view of a classic tracker on top of per-channel patterns.
- **Chord shorthand:** a compact way to write a chord in one cell, in the spirit
  of the old trackers' arpeggio effect (`0xy`), that expands to several voices or
  an arpeggio.
- **Audio mute:** a fast gain fade instead of the musical mute, for choppy,
  stuttering effects.
- **Instrument override table** per channel, for swapping instruments.
- **Reverb on MCUs.**
- **Pattern events:** an effect command in a pattern fires a named event. Mappings
  or Lua handle it, and can then change any target. This is how a pattern
  influences other channels or tempo without writing to them directly. Other
  possible uses: "safe to transition here" markers, and triggering stingers.

## Open questions
- Key change: when the scale changes size (7-note to pentatonic), wrap the degree
  or snap to the nearest scale note? Decide by listening tests.
