# Song file format

A song is one JSON file (UTF-8). It holds everything needed to play the song
and to keep editing it: the engine's data plus names and stable ids. The app
reads and writes it with `core/songfile` (`adaptive::songfile::load` / `save`);
`songs/adaptive-demo.json` is an example, written from the demo's code by
`build/export_demo`.

The file is the app's own format. Microcontroller builds will take a song
converted ahead of time into a compact binary, so that format can change
freely without breaking saved songs.

## Compatibility rules

- `format` is always `"adaptive-tracker-song"`; `version` is a whole number,
  currently 2. Until the format settles, the app reads only its own version
  and refuses others, saying whether the file is older or newer.
- Readers ignore fields they don't know. Additions that a reader may safely
  ignore don't change `version`; anything else does.
- Missing optional fields take the defaults below. Instrument parameters
  left out take the synth's defaults.
- Writers write every field they know, including instrument parameters at
  their defaults, so a song doesn't change if a default changes later.
- Things refer to each other by **id**: lowercase words joined with `-`
  (`"explore-lead"`), unique within their kind (pattern ids are unique in the
  song). Ids never need to change when a name does. Order in a list matters
  only where noted.
- Errors name the place in the file: `sections[0].channels.lead[0]: no pattern 'x'`.

## Top level

| Field | | |
|---|---|---|
| `format`, `version` | required | see above |
| `title` | text | |
| `tempo` | BPM, default 120 | tempo at the start |
| `key` | key, default C major | the key the song is written in (degree patterns resolve against it) |
| `start` | section id | the section Play starts; default the first |
| `instruments` | list | synth patches |
| `channels` | list | each with its columns and its patterns |
| `params` | list | values the app sets, 0..1 |
| `sections` | list | which patterns each channel plays |
| `layers` | list | groups of columns, muted and faded together; columns say which layers they are in |
| `events` | list | named triggers and what they do |
| `mappings` | list | how parameters drive the music |

Every item in the lists has `id` (required) and `name` (default: the id).

A **key** is `{"root": "C", "scale": "major"}`. Roots are note names with `#`
or `b`. Scales are `major`, `minor`, `harmonicMinor`, `dorian`, `phrygian`,
`lydian`, `mixolydian`, `locrian`, `majorPentatonic`, `minorPentatonic`,
`chromatic`, or a list of semitones from the root, e.g. `[0, 2, 3, 7, 8]`.

## Instruments

```json
{"id": "bass", "name": "Bass", "voice": "subtractive", "params": {"shape": 0, "cutoff": 320, "resonance": 3, ...}}
```

`pitch` (optional) is a hint for editing: a column given this instrument
switches to that pitch mode (see columns). Playback ignores it.

`voice` is the kind of sound source; it decides what `params` means. A
reader refuses a voice it doesn't know ("needs a newer version") rather than
playing the instrument with the wrong one, so new voices can be added
without changing `version`. Absent means `subtractive`.

Voices:

**`subtractive`**: pressense's oscillator, filter and envelopes. `params` uses the engine's parameter keys (`shape`, `cutoff`, `resonance`,
`filterEnvAmount`, `filterAttack`, `filterDecay`, `filterSustain`,
`filterRelease`, `ampAttack`, `ampDecay`, `ampSustain`, `ampRelease`, `level`,
`vibratoRate`, `vibratoDepth`, `tremoloRate`, `tremoloDepth`, `filterMode`,
`noiseMix`, `noiseDecay`, `pitchEnv`) in natural units:
Hz, seconds, 0..1. Values outside the synth's range are errors. Parameters
left out take the synth's defaults, so a song from before one existed plays
as it did.

The last four are for percussion:

- `filterMode`: 0 low-pass (the default), 1 high-pass, 2 band-pass.
- `noiseMix`: 0 is the oscillator alone (the default), 1 is white noise
  alone. The noise joins ahead of the filter, so `filterMode` and `cutoff`
  shape it: high-pass for hats, band-pass for shakers.
- `noiseDecay`: seconds for the noise to fall 60 dB after each note starts,
  on top of the amp envelope. The maximum, 10, is "hold": no decay of its own
  (the default).
- `pitchEnv`: semitones the filter envelope sweeps the pitch at its peak,
  -48..48 (default 0). With the filter envelope's sustain at 0 the sweep
  settles on the played note, as a kick or a tom does.

## Channels and patterns

```json
{
  "id": "drums", "name": "Drums", "volume": 0.8,
  "columns": [{"instrument": "kick", "pitch": "absolute", "layers": ["rhythm"]},
              {"instrument": "hat", "pitch": "absolute",
               "playsWhen": {"param": "intensity", "atLeast": 0.5}}],
  "patterns": [
    {"id": "combat-drums", "name": "Combat drums", "rows": 16, "ticksPerRow": 6,
     "cells": [
       {"row": 0, "column": 0, "note": "A2", "volume": 127, "effect": "260"},
       {"row": 2, "column": 1, "note": "C8", "volume": 60}
     ]}
  ]
}
```

- `volume` (optional, default 1): the mixer fader, 0 to 1, heard as its
  square (1 is unity, 0.5 is a quarter).
- `columns`: one per voice, 1 to 4. The number of columns is the
  channel's; all its patterns have that many. Each column has:
  - `pitch` (required): how its cells' notes are written: `absolute` (MIDI
    notes that never move), `transpose` (notes that follow key changes), or
    `degree` (scale degrees, resolved against the key).
  - `instrument` (optional): the instrument it plays.
  - `playsWhen` (optional), like `{"param": "intensity", "atLeast": 0.5}`:
    while the parameter is below the value, the column starts no new notes
    (what's sounding ends at its next note). Absent: it always plays.
  - `layers` (optional): ids of the layers the column is in.
- A pattern belongs to its channel. `rows`: 1 to 256. `ticksPerRow` (speed):
  effect ticks per row, 24 per beat, so 6 is four rows per beat, 12 two, 24
  one; values that don't divide 24 (like 5) are fine. Slides, sweeps and
  arpeggios move once per tick.

**Cells** list only rows with something in them, by `row` and `column`
(both from 0). A cell has at most one of:

- `"note": "C#4"`: absolute and transpose channels. Middle C is C4.
- `"degree": 1, "octave": 4`: degree channels. Degree 1 is the key's root;
  degrees past the scale's length wrap into the next octave. Optional
  `"accidental"`: -1 flat, 1 sharp.
- `"off": true`: release the note.

and optionally `"volume"` (0 to 127; absent = no change) and `"effect"`: three
hex digits, the command then its parameter (`"024"` arpeggio, `"F80"` cutoff;
see DESIGN.md's effect table). A parameter typed before its command is
written `".24"`.

Cells in columns past the channel's column count are kept, so removing a
column and adding it back loses nothing.

## Sections

```json
{"id": "combat", "name": "Combat", "tempo": 128, "beatsPerBar": 4,
 "channels": {"lead": ["combat-lead"], "drums": ["combat-drums", "combat-fill"]}}
```

`channels` maps a channel id to the patterns it plays in this section, in
order, looping. Each channel loops on its own, so sequences of different
lengths drift against each other. A channel left out is silent. Patterns
must belong to the channel. `tempo` (BPM) changes the tempo on entry
(absent: keep it). `beatsPerBar` (default 4) sets where bars fall, for
changes quantised to the bar. A section with `bridgeBeats` is a bridge: when
a transition goes `via` it, it plays for that many beats, then the
destination starts.

## Parameters, layers and events

```json
"params": [{"id": "danger", "name": "Danger", "initial": 0}],
"layers": [{"id": "rhythm", "name": "Rhythm"}],
"events": [{"id": "alarm", "name": "Alarm", "actions": [
  {"type": "gotoSection", "section": "combat", "when": "now"},
  {"type": "playStinger", "pattern": "hit-stinger", "when": "now"}
]}]
```

Up to 4 actions per event, run in order:

| `type` | fields | `when` default |
|---|---|---|
| `gotoSection` | `section`; `entry` `restart` (default) or `keepPhase`; `via` a bridge section | `bar` |
| `playStinger` | `pattern` (plays once on the pattern's channel) | `now` |
| `setLayerMute` | `layer`, `muted` (default true) | `bar` |
| `setParam` | `param`, `value` 0..1 | (immediate) |
| `setChannelMute` | `channel`, `muted` (default true): silent at once, keeps its place | `now` |
| `setChannelSolo` | `channel`, `soloed` (default true): channels not soloed are silent | `now` |
| `setChannelPause` | `channel`, `paused` (default true): starts no notes, keeps its place | `bar` |
| `restartChannel` | `channel`: plays its sequence from the top | `bar` |
| `bumpBack` | `to` `bar` (default: every channel replays the bar playing, and the bar count starts over) or `pattern` (each channel's pattern from its first row) | `now` |
| `setKey` | `root`, `scale` as in a key | `bar` |
| `setTempo` | `tempo` (BPM) | `beat` |
| `stop` | | (immediate) |

`when` is `now`, `beat` or `bar`.

## Mappings

```json
{"param": "intensity", "target": "layerGain", "layer": "rhythm",
 "curve": "linear", "from": 0.15, "to": 1, "rampMs": 400},
{"param": "danger", "target": "threshold", "threshold": 0.75,
 "above": "enter-combat", "below": "calm-down"}
```

A mapping turns a parameter (0..1) into a value from `from` (at 0) to `to`
(at 1), with a `linear` or `exponential` `curve` (exponential for Hz and
tempo; both ends above 0), gliding over `rampMs` (default 30). Targets:
`channelGain` and `channelCutoff` (with `channel`), `layerGain` (with
`layer`), `masterCutoff`, `tempoScale` (multiplies the tempo), `speed` (tempo and pitch together, like tape: 0.5 is half speed, an octave down), `transpose`
(semitones). A `threshold` mapping fires event `above` when the parameter
rises past `threshold`, and `below` when it falls back.

## Limits

Set at build time in `core/engine/include/adaptive/config.hpp`; a file over
them won't load (and says which limit). Currently 8 channels, 4 columns,
256 rows per pattern, 64 patterns, 64 patterns per sequence, 16 sections,
8 layers, 16 events, 16 parameters, 16 instruments, 32 mappings.
