# Adaptive Tracker — Design

An Android app for composing **adaptive music**: music that reacts to changing
state, for games, theatre, live performance or anything else. It combines a
tracker-style sequencer, the Pressense synth engine, and Lua scripting.
Composed on a phone; the playback core must also be able to run on
microcontrollers (ESP32, RP2350) later.

## Decisions so far

| Topic | Decision |
|---|---|
| Synth | Pressense `lib/synth`, synth-only for now (no samples). Repo: `git@github.com:jfuerth/pressense.git`. (The local checkout at `../pressence/pressence` has a misspelled directory name.) |
| Pressense integration | **Git submodule of the whole repo for now**, using `lib/synth`, `lib/features` and `lib/nlohmann`. Move `lib/synth` into its own repo later, once its interface has settled for this project. Optional upstream cleanup: move the JSON conversion out of `performance_timer.hpp` so the synth no longer needs nlohmann |
| Programmatic control | Lua, chosen for realistic overhead on MCUs |
| Export / embeddable runtime | Not yet. Phone-only for now, but the core stays portable |
| UI | Kotlin + Jetpack Compose; C++ core behind JNI |
| Adaptive effects (v1) | Channel mute/unmute, channel effects, master effects (filter, reverb), global tempo, instrument parameter changes, key change |
| Mute | Musical mute first: stop new notes, let current notes release, quantised. A click-free "audio mute" fade may come later, e.g. for choppy effects |
| Reverb | Phone only for now. Not required on MCUs |
| Instrument changes | Parameter changes only. A per-channel instrument override table may come later |
| Instruments | `adaptive::Instrument` is a flat array of 17 pressense parameters in natural units (shape, cutoff, resonance, filter envelope + amount, amp envelope, level, vibrato, tremolo). The song holds a table of them; **each column of each channel names one** (`Song::columnInstrument`), e.g. the drum channel's kick and hat columns. No instrument field in cells. Editing is a `SetInstrumentParam` command that updates every voice using the instrument, including held previews. Previews play an instrument, not a channel's voice. **No copies of pressense in the app:** instrument defaults are read from a default-constructed voice; the editor gets parameter keys and ranges from the engine; the waveform and envelope graphs are rendered by pressense's own oscillator and envelope (`adaptive/inspect.hpp`); effect descriptions get their values from the engine's `fxmap`. While the song plays, the audio thread owns it: new instruments are written by the app into an unused slot, then made live with `AddInstrument`; `AssignInstrument` changes a column's instrument; the UI keeps its own view of the count and assignments. Names live in the app. **Monitor:** `SetMonitor` picks an instrument whose voices (columns playing it and its previews, before channel filter/gain and the master stage) `render()` also writes to a second buffer; the app shows it as an oscilloscope and a live spectrum (level against log frequency, analyser-style fall-back) |
| Instrument editor | The synth is drawn as its **signal flow**: oscillator → filter → amp down the right, modulators (vibrato, filter envelope, amp envelope, tremolo) on the left with arrows into the stage they modulate; modulation amounts sit on the arrows. Tapping a value opens its slider (log curve for Hz and times) with fine-step buttons; the 12-key keyboard plays the instrument while you adjust it. Envelopes and the oscillator shape are drawn |
| Chords | Several columns per channel, one voice per column. No chord shorthand yet |
| Pattern scope | **Per channel.** Each pattern belongs to one channel (its pitch mode, columns and instruments). Each channel plays its own sequence of patterns, which can have different lengths. A section says which pattern(s) each channel plays |
| Columns | **Fixed per channel**, 1-4, in the song: every pattern on the channel has that many. Patterns store all 4, so removing a column and adding it back loses nothing: removing one moves later columns left and parks it just past the end. Length and speed can change while a pattern plays: the channel carries on from the current row at the new speed, and later sequence entries keep their places. Every shape edit is a Song method, used by both the engine (via a command) and the app's own copy, so they can't disagree |
| Song data | `adaptive::Song` holds everything the engine plays, by index, with no pointers: channel setups, a pool of patterns, sections with per-channel sequences, layers, events, parameters (with initial values), mappings, instruments, start tempo, key and section. The engine changes it only on command, on the audio thread |
| Song file | **JSON**, see `docs/song-format.md`. Tolerant reader (unknown fields ignored, defaults for missing ones), refuses newer versions, references by stable id, names separate from ids, errors say where. Instruments carry a `voice` type that decides what their parameters mean; readers refuse voices they don't know, so new voices need no version bump. The app's own format; MCU builds will take a converted binary, so the MCU side can change freely. The demo is a file now (`songs/adaptive-demo.json`, bundled as an app asset, written by `export_demo` from `core/demo/adaptive_demo.hpp`; a test fails if they drift) |
| Memory (MCU, later) | Patterns are fixed at 256 rows x 4 columns (5 KB each), fine on the phone but not for an MCU's pattern pool. The file stores only what's used; an MCU build will want a shared cell arena |
| Timing | Engine clock in ticks at 96 PPQ; effects move every 4 ticks (24 **effect ticks** per beat). Each pattern sets its own **length in rows** and **speed: effect ticks per row**, as in trackers (6 = four rows per beat; 5 is fine too, rows needn't line up with beats), so channels can play polyrhythms. **Beats per bar is set per section** and defines bar quantisation, counted from section start. Patterns needn't fill whole bars |
| Tempo | Comes only from the section plus adaptive mappings (a multiplier). **Patterns cannot change tempo** |
| Degree mapping | **Wrap** is the default (chosen by listening test, 2026-11-12). Snap stays available per song |
| Section transitions | Each transition chooses **Restart** (default: channels start their new sequences from the top) or **Keep phase** (channels continue at the same position, and the bar count carries on). A transition can play a **bridge** section once before entering the target |
| UI wording | **Never say "game" in the GUI.** The composer names parameters and events; the music may be for theatre, live performance or anything else. Example names like Intensity, Danger, Hit are fine |
| Events | Named one-shot triggers (buttons in the app; later a host application or Lua). Each fires a short list of actions: play a stinger, go to a section, mute/unmute a layer. Actions are immediate by default, or quantised to the next beat or bar |
| Pattern editor | Swipe to scroll; edits happen on a fixed **edit row** in the middle. **12 keys** across most of the screen width; keys sound as pressed; a multi-finger chord becomes the lowest note plus `0xy` arpeggio (scale steps in Degree mode), written when all fingers lift. Effects are entered on a **2×8 hex pad** with a **live description line**. The cursor stops on the note and on **each hex digit** (volume hi/lo, command, x, y). The keyboard writes notes; the hex pad writes the digit under the cursor (the command when on the note) and moves to the next digit of the group; after the group's last digit the cursor returns to where typing started and the row advances by the edit step, so selecting one digit and typing tweaks just that digit. The edit bar is fixed in the middle of the viewport; rows scroll past it. Swiping left/right anywhere in the pattern moves the cursor one stop per 32 dp (note → each digit → next column). Drags and flings snap a row onto the edit bar. The editor's Play button plays the **Song**, the **Pattern** alone (looping), or one **Column** of it. Portrait first; a landscape layout with a wider keyboard is a later option |
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
| **Parameter** | A named float set by a UI slider, a script, or (later) a host application. | |
| **Target** | Any addressable adaptive value, e.g. `channel[2].mute`, `channel[2].filter.cutoff`, `master.filter.cutoff`, `tempo`, `instrument["lead"].filter_q`, `key.root`. | |
| **Mapping** | Data that drives a target from a parameter (curve, range, ramp, quantise). | |

## Layers

```
┌──────────────────────── Android app (Kotlin/Compose) ────────────────────────┐
│ Pattern grid editor · Patch editor · Parameter sliders · Lua script editor    │
└───────────────────────────────┬──────────────────────────────────────────────┘
                                │ JNI (commands in, state snapshots out)
┌───────────────────────────────▼──────────────────────────────────────────────┐
│ core/control  (control thread, MCU core 0)                                    │
│   Lua VM (optional) — runs at tick/row rate, never on the audio thread        │
│   Parameters (named floats) · rule evaluation                                 │
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

## Adaptive model (implemented in milestone 4)

Data lives in `adaptive::Song` (`core/engine/include/adaptive/song.hpp`). The
engine refers to sections, layers, events and parameters by index. Their names
live in the app.

- **Commands** reach the audio thread through a lock-free single-producer queue
  (`Engine::post`). Each command can be quantised: **now**, **next beat** or
  **next bar**. Beats and bars are counted from the start of the current section.
  Commands: go to section, play stinger, mute/unmute layer, fire event, set
  parameter, set key, set tempo, stop.
- **Sections** give each channel a pattern sequence (or none), a beats-per-bar
  value and an optional tempo. Going to a section uses **Restart** or **Keep
  phase**, optionally via a **bridge** section that plays for its `bridgeBeats`
  first. The latest section request cancels any pending one.
- **Layers** are channel bitmasks. **Musical mute**: muted channels start no new
  notes, sounding notes release, and the sequence keeps its place.
- **Stingers** play a pattern once on a channel kept out of sections. Section
  changes leave a playing stinger alone.
- **Events** fire up to four commands, each with its own quantisation.
- **Parameters** (0–1) drive **mappings**: channel gain, layer gain,
  channel low-pass cutoff, master cutoff, tempo scale or transpose. Curves are
  linear or exponential, with a glide time. A **threshold** mapping fires an
  event when the parameter rises past a value, and another when it falls back.
  When a song loads, starting values apply instantly.
- **Channel strip:** low-pass filter (off at 18 kHz and above) and a smoothed gain
  (strip × layers). Pan waits for stereo output.
- **Demo:** `core/demo/adaptive_demo.hpp` (Explore, Combat, a bridge, three
  layers, Intensity and Danger, Hit/Pickup/Alarm events).
  `desktop/adaptive_render` plays it through a scripted timeline to a WAV.

## Android audio and threading (milestone 3)

- Oboe output stream: float, mono, low-latency, exclusive if available, at a fixed
  48 kHz. Oboe resamples if the device runs at another rate. Buffer = 2 bursts.
- The engine is only touched on the audio thread. The UI thread posts commands
  to the engine's lock-free queue, and is its only producer. The audio thread
  publishes state for the UI (current section, underruns, timing) through atomics.
- New playback starts at `Engine::nextTick()` (at most one tick, about 2 ms, later).
- If the stream dies (e.g. headphones unplugged), it reopens unless the app
  closed it meanwhile. No background playback yet; that needs a foreground
  service.

## Pitch and keys

Each channel has a **pitch mode** (`core/engine/include/adaptive/pitch.hpp`):

| Mode | Cell note means | Follows key root / transpose | Follows scale |
|---|---|---|---|
| Absolute | MIDI note | No | No |
| Transpose | MIDI note | Yes | No |
| Degree | octave × 12 + scale-degree index, plus an accidental (±1) | Yes | Yes |

- Scales are 12-bit masks (major, minor, harmonic minor, the modes, pentatonics,
  chromatic). A key is a root (pitch class) plus a scale.
- The engine keeps a **home key** (the key the song is written in) and a
  **current key**. A root change moves by the shortest route (−5 to +6
  semitones), so a key change never jumps by more than a tritone.
- Pitches are resolved when a note starts; sounding notes keep their pitch.
- When the current scale has a different size from the home scale, a **degree
  mapping** decides what a degree means: **Wrap** (degree n is the n-th note of
  the current scale, wrapping into the next octave) or **Snap** (take the home
  scale's note and move it to the nearest note of the current scale, ties going
  down). With scales of equal size, both map degree for degree.
- In Degree mode the arpeggio's x and y are scale steps, so `0x24` plays the
  triad of whatever scale is current.
- `desktop/key_experiment` renders one arrangement in nine keys/scales/mappings
  for listening.

## Effects

Effects follow ProTracker conventions where they make sense. Notes, volume and
the parameter effects apply on a row's first tick. Slides and arpeggios update
on every later **effect tick** of the row. An effect tick is 4 engine ticks
(1/24 beat), which is ProTracker's default rate at 125 BPM and follows the tempo.
A row with no effect ends the previous row's arpeggio or volume slide. **Pitch
slides and filter sweeps persist** across rows (a deliberate break from
ProTracker): `1xx`/`2xx` until `100`/`200`, another pitch command or a new
note; `3xx` until it reaches its note; `8xx`/`9xx` until another filter
command. A motion carried over from an earlier row also steps on row starts,
so it moves evenly. For `3` and `A`, a parameter of `00` reuses the last
nonzero one.

Every effect command is a single hex digit, so the editor's hex pad enters the
command and its two parameter digits.

| Code | Effect | Parameter |
|---|---|---|
| `0xy` | Arpeggio: note, +x, +y, cycling each effect tick | Semitones (scale steps in Degree mode) |
| `1xx` | Pitch slide up (persists; `100` stops) | xx/16 semitone per effect tick |
| `2xx` | Pitch slide down (persists; `200` stops) | xx/16 semitone per effect tick |
| `3xx` | Glide to the row's note (no retrigger, no overshoot; carries on across rows until it arrives) | xx/16 semitone per effect tick |
| `4xy` | Vibrato (persists; `400` turns it off) | x = rate 0.5–8 Hz, y = depth in 1/10 semitone |
| `5xx` | Filter resonance (persists) | Quadratic over the synth's Q range |
| `6xx` | Waveform shape (persists) | 00 … FF = 0 … 1 |
| `8xx` | Filter sweep up (persists across rows until `F`, `8` or `9`, or a restart; `800` stops) | xx/16 `Fxx` steps per effect tick: `810` ≈ 0.94 octave per beat. Stops at 20 kHz |
| `9xx` | Filter sweep down (as `8xx`; `900` stops) | As `8xx`. Stops at 20 Hz |
| `Axy` | Volume slide: up x, else down y | 1/64 of full volume per effect tick |
| `Bxx` | Fire event xx | Event index. Fires on the row's first tick; the event's own actions set their timing |
| `Cxx` | Set volume | 00 … 7F |
| `Fxx` | Filter cutoff (voice baseline, persists) | 00 = 20 Hz … FF = 20 kHz, exponential |

`7` (tremolo), `D` and `E` are reserved. A sweep continuing from an earlier row also steps on row starts, so it moves evenly across rows. Pan, once output is stereo, will be a channel-strip control.

The volume column (0–127) sets the column's volume. A note without a volume
starts at full volume. Patterns cannot change tempo (see Decisions).

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
  tests/         host unit tests (doctest), portability compile check
desktop/         host tools: render_demo, key_experiment (render to WAV)
android/         Gradle project: app (Compose) + :native (CMake → core + Oboe)
```

## First milestones

1. ✅ **Core skeleton + host tests** (done 2026-11-12): pressense submodule, one channel playing a
   hard-coded pattern through `WavetableSynth`, offline-rendered to WAV.
2. ✅ **Sequencer** (done 2026-11-12): per-channel pattern sequences of mixed lengths
   and speed, the effects above, pitch modes and keys, several channels.
   Listening test result: Wrap is the default degree mapping.
3. ✅ **Android shell** (done 2026-11-12): Gradle + NDK + Oboe; play/stop a demo song from the phone.
4. ✅ **Adaptive director** (done 2026-11-12): command queue, sections and
   transitions (Restart / Keep phase / bridge), layers with musical mute,
   stingers, events, parameters and mappings, channel filter and gain.
5. **Editors.** ✅ Pattern editor (done 2026-11-12): edit row, 12-key multi-touch
   keyboard with previews, chords to arpeggios, 2×8 hex pad with live
   descriptions, follow mode; edits are safe while playing. ✅ Instrument editor
   (done 2026-11-12): signal-flow layout, tap-to-slide, live while playing.
   ✅ Creating, duplicating and renaming instruments, and assigning them to
   columns (from the column headings or the instrument's "Plays on" list);
   live oscilloscope and spectrum of the instrument's output (done
   2026-11-13). Still to do: column count, pattern length and speed
   editing, creating patterns and arranging sequences.
6. **Lua**: control thread, API, script editor; measure footprint.
7. ✅ Song file format (done 2026-11-13): JSON load/save, the demo as a
   file; instruments name their voice type, and unknown voices are refused.
   The app autosaves to private storage (a few seconds after edits and when
   it goes to the background) and exports/imports files through the system
   file picker. Saving reads the native player's UI-thread copy of the song,
   which every edit updates alongside the engine's, so it never races the
   audio thread. Later: the MCU
   build (RP2350 first, reusing pressense's I2S sink) with a converted binary song.

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
- **Instrument field in cells:** let a note pick its instrument, so one column can
  play kick, snare and hat. Costs a byte per cell.
- **Aftertouch-style modulation** in instruments: pressense voices have per-parameter
  pressure amounts; a pattern effect or parameter mapping could drive them.
- **Reverb on MCUs.**
- **Pattern events:** an effect command in a pattern fires a named event. Mappings
  or Lua handle it, and can then change any target. This is how a pattern
  influences other channels or tempo without writing to them directly. Other
  possible uses: "safe to transition here" markers, and triggering stingers.

## Open questions
