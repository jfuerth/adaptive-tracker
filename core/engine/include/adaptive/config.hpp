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

/// Mixer strips.
inline constexpr uint32_t kMaxChannels = ADAPTIVE_MAX_CHANNELS;
/// Monophonic note lanes per channel; one synth voice each.
inline constexpr uint32_t kMaxColumns = ADAPTIVE_MAX_COLUMNS;
/// Rows per pattern.
inline constexpr uint32_t kMaxRows = ADAPTIVE_MAX_ROWS;

} // namespace adaptive::config
