#pragma once

#include <cstdint>

/**
 * Compile-time capacities for the engine.
 *
 * Every container in core/engine is fixed-size and sized from these, so the RAM
 * budget is known at compile time. MCU builds can override them with -D flags.
 */
namespace adaptive::config {

/// Engine clock resolution in ticks per beat (quarter note). 96 is divisible by
/// 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48 and 96, so it covers straight and triplet
/// subdivisions down to 1/32 notes.
inline constexpr uint32_t kTicksPerBeat = 96;

#ifndef ADAPTIVE_MAX_CHANNELS
#define ADAPTIVE_MAX_CHANNELS 8
#endif
#ifndef ADAPTIVE_MAX_COLUMNS
#define ADAPTIVE_MAX_COLUMNS 4
#endif
#ifndef ADAPTIVE_MAX_ROWS
#define ADAPTIVE_MAX_ROWS 256
#endif

#ifndef ADAPTIVE_MAX_PATTERNS
#define ADAPTIVE_MAX_PATTERNS 64
#endif

#ifndef ADAPTIVE_MAX_SEQUENCE_LENGTH
#define ADAPTIVE_MAX_SEQUENCE_LENGTH 64
#endif

#ifndef ADAPTIVE_MAX_SECTIONS
#define ADAPTIVE_MAX_SECTIONS 16
#endif
#ifndef ADAPTIVE_MAX_LAYERS
#define ADAPTIVE_MAX_LAYERS 8
#endif
#ifndef ADAPTIVE_MAX_EVENTS
#define ADAPTIVE_MAX_EVENTS 16
#endif
#ifndef ADAPTIVE_MAX_EVENT_ACTIONS
#define ADAPTIVE_MAX_EVENT_ACTIONS 4
#endif
#ifndef ADAPTIVE_MAX_PARAMS
#define ADAPTIVE_MAX_PARAMS 16
#endif
#ifndef ADAPTIVE_MAX_INSTRUMENTS
#define ADAPTIVE_MAX_INSTRUMENTS 16
#endif
#ifndef ADAPTIVE_MAX_MAPPINGS
#define ADAPTIVE_MAX_MAPPINGS 32
#endif
#ifndef ADAPTIVE_MAX_PENDING_COMMANDS
#define ADAPTIVE_MAX_PENDING_COMMANDS 32
#endif

/// Engine ticks per effect tick. 96 / 4 = 24 effect ticks per beat, which is
/// ProTracker's default rate (6 ticks per row at 4 rows per beat), so slides and
/// arpeggios behave the way tracker users expect and follow the tempo.
inline constexpr uint32_t kTicksPerEffectTick = 4;

/// Mixer strips.
inline constexpr uint32_t kMaxChannels = ADAPTIVE_MAX_CHANNELS;
/// Monophonic note lanes per channel; one synth voice each.
inline constexpr uint32_t kMaxColumns = ADAPTIVE_MAX_COLUMNS;
/// Rows per pattern.
inline constexpr uint32_t kMaxRows = ADAPTIVE_MAX_ROWS;
/// Entries in a channel's pattern sequence.
inline constexpr uint32_t kMaxPatterns = ADAPTIVE_MAX_PATTERNS;
inline constexpr uint32_t kMaxSequenceLength = ADAPTIVE_MAX_SEQUENCE_LENGTH;
/// Effect ticks per beat: a pattern's speed is counted in these.
inline constexpr uint32_t kEffectTicksPerBeat = kTicksPerBeat / kTicksPerEffectTick;
inline constexpr uint32_t kMaxSections = ADAPTIVE_MAX_SECTIONS;
inline constexpr uint32_t kMaxLayers = ADAPTIVE_MAX_LAYERS;
inline constexpr uint32_t kMaxEvents = ADAPTIVE_MAX_EVENTS;
inline constexpr uint32_t kMaxEventActions = ADAPTIVE_MAX_EVENT_ACTIONS;
/// Parameters (named floats 0..1; names live in the app).
inline constexpr uint32_t kMaxParams = ADAPTIVE_MAX_PARAMS;
inline constexpr uint32_t kMaxMappings = ADAPTIVE_MAX_MAPPINGS;
/// Instruments (synth patches) in a song.
inline constexpr uint32_t kMaxInstruments = ADAPTIVE_MAX_INSTRUMENTS;
/// Commands waiting for their quantised time (e.g. "at the next bar").
inline constexpr uint32_t kMaxPendingCommands = ADAPTIVE_MAX_PENDING_COMMANDS;
/// Voices for auditioning notes in the editor (one per finger on the keyboard).
inline constexpr uint32_t kPreviewVoices = 4;
/// Commands in flight from the UI/control thread to the audio thread (power of two).
inline constexpr uint32_t kCommandQueueSize = 64;

} // namespace adaptive::config
