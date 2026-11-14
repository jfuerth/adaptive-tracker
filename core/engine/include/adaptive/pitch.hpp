#pragma once

#include <cstdint>
#include <initializer_list>

/**
 * Keys, scales and pitch resolution.
 *
 * A channel's PitchMode says how its notes respond to the current key:
 * - Absolute:  notes are MIDI numbers and never move (drums, fixed drones).
 * - Transpose: notes are MIDI numbers that follow changes of key root and the
 *              global transpose, but not changes of scale.
 * - Degree:    notes are scale degrees, resolved against the current key, so a
 *              change from major to minor moves the 3rd, 6th and 7th.
 *
 * Pitches are resolved when a note starts; notes already sounding keep their pitch.
 * Everything here is integer-only and constexpr.
 */
namespace adaptive {

/// Scales as 12-bit masks: bit i set means the scale contains the note i
/// semitones above the root.
namespace scales {
constexpr uint16_t mask(int a, int b, int c, int d, int e, int f = -1, int g = -1) {
    uint16_t m = 0;
    for (int s : {a, b, c, d, e, f, g}) {
        if (s >= 0) m = static_cast<uint16_t>(m | (1u << s));
    }
    return m;
}
inline constexpr uint16_t kMajor = mask(0, 2, 4, 5, 7, 9, 11);
inline constexpr uint16_t kNaturalMinor = mask(0, 2, 3, 5, 7, 8, 10);
inline constexpr uint16_t kHarmonicMinor = mask(0, 2, 3, 5, 7, 8, 11);
inline constexpr uint16_t kDorian = mask(0, 2, 3, 5, 7, 9, 10);
inline constexpr uint16_t kPhrygian = mask(0, 1, 3, 5, 7, 8, 10);
inline constexpr uint16_t kLydian = mask(0, 2, 4, 6, 7, 9, 11);
inline constexpr uint16_t kMixolydian = mask(0, 2, 4, 5, 7, 9, 10);
inline constexpr uint16_t kLocrian = mask(0, 1, 3, 5, 6, 8, 10);
inline constexpr uint16_t kMajorPentatonic = mask(0, 2, 4, 7, 9);
inline constexpr uint16_t kMinorPentatonic = mask(0, 3, 5, 7, 10);
inline constexpr uint16_t kChromatic = 0x0FFF;
} // namespace scales

enum class PitchMode : uint8_t { Absolute, Transpose, Degree };

/// What a degree means when the current scale has a different number of notes
/// from the song's home scale (e.g. 7-note major to 5-note pentatonic).
enum class DegreeMapping : uint8_t {
    /// Degree n is the n-th note of the current scale, wrapping into the next
    /// octave past the end. Degree 5 in a pentatonic scale is the next octave's root.
    Wrap,
    /// Degree n is the n-th note of the home scale, moved to the nearest note of
    /// the current scale (ties go down). Keeps melodic contour. When both scales
    /// have the same size this behaves like Wrap.
    Snap,
};

struct Key {
    uint8_t root = 0;                  ///< Pitch class 0-11 (0 = C).
    uint16_t scale = scales::kMajor;   ///< See scales.
};

/// Everything needed to turn a cell's note into a MIDI pitch.
struct PitchContext {
    Key home;                  ///< The key the song is written in.
    Key current;               ///< The key now in effect.
    int8_t transpose = 0;      ///< Extra semitones for Transpose and Degree channels.
    DegreeMapping mapping = DegreeMapping::Wrap;
};

constexpr int scaleSize(uint16_t scale) {
    int n = 0;
    for (int i = 0; i < 12; ++i) n += (scale >> i) & 1;
    return n;
}

/// Semitones above the root of the scale's `index`-th note (0 <= index < size).
constexpr int scaleNoteSemitones(uint16_t scale, int index) {
    for (int i = 0; i < 12; ++i) {
        if ((scale >> i) & 1) {
            if (index == 0) return i;
            --index;
        }
    }
    return 0;
}

/// Shortest signed distance from one root to another, in -5..+6 semitones, so a
/// key change never jumps by more than a tritone.
constexpr int rootShift(uint8_t fromRoot, uint8_t toRoot) {
    int d = ((static_cast<int>(toRoot) - static_cast<int>(fromRoot)) % 12 + 12) % 12;
    return d > 6 ? d - 12 : d;
}

namespace detail {
constexpr int floorDiv(int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }
constexpr int clampMidi(int n) { return n < 0 ? 0 : (n > 127 ? 127 : n); }

/// The note of `scale` nearest to `semitones` (any octave), ties going down.
constexpr int snapToScale(uint16_t scale, int semitones) {
    int best = semitones;
    int bestDist = 99;
    for (int octave = -1; octave <= 1; ++octave) {
        for (int i = 0; i < 12; ++i) {
            if (!((scale >> i) & 1)) continue;
            const int candidate = i + 12 * octave;
            const int dist = candidate > semitones ? candidate - semitones : semitones - candidate;
            if (dist < bestDist || (dist == bestDist && candidate < best)) {
                best = candidate;
                bestDist = dist;
            }
        }
    }
    return best;
}
} // namespace detail

/**
 * Pitch of a scale degree. `degree` may be negative or past the end of the
 * scale; it then moves into lower or higher octaves. Octave 4, degree 0 is the
 * key's root in the octave of middle C (C4 = MIDI 60).
 */
constexpr int resolveDegree(int octave, int degree, int accidental, const PitchContext& ctx) {
    const int base = 12 * (octave + 1) + ctx.home.root + rootShift(ctx.home.root, ctx.current.root) +
                     ctx.transpose;
    const int curSize = scaleSize(ctx.current.scale);
    const int homeSize = scaleSize(ctx.home.scale);
    int semitones = 0;
    if (ctx.mapping == DegreeMapping::Snap && curSize != homeSize) {
        const int q = detail::floorDiv(degree, homeSize);
        const int homeSemis = scaleNoteSemitones(ctx.home.scale, degree - q * homeSize);
        semitones = detail::snapToScale(ctx.current.scale, homeSemis) + 12 * q;
    } else {
        const int q = detail::floorDiv(degree, curSize);
        semitones = scaleNoteSemitones(ctx.current.scale, degree - q * curSize) + 12 * q;
    }
    return detail::clampMidi(base + semitones + accidental);
}

/**
 * The scale degree that plays MIDI note `midi` in `key` (the inverse of
 * resolveDegree with no key change): the note byte (octave * 12 + degree)
 * and an accidental of -1, 0 or 1. False if the note is out of range.
 */
constexpr bool encodeDegree(int midi, const Key& key, uint8_t& note, int8_t& accidental) {
    const int offset = midi - 12 - key.root;  // semitones above the root of octave 0
    int octave = detail::floorDiv(offset, 12);
    const int pc = offset - 12 * octave;
    const int size = scaleSize(key.scale);
    // The scale note at or below, and the one above (the next octave's root past the top).
    int below = 0;
    int index = 0;
    for (int i = 0; i < size; ++i) {
        if (scaleNoteSemitones(key.scale, i) <= pc) {
            below = scaleNoteSemitones(key.scale, i);
            index = i;
        }
    }
    int degree = index;
    int acc = pc - below;
    if (acc > 1) {  // nearer the next scale note: flatten it
        degree = index + 1;
        acc = pc - (index + 1 < size ? scaleNoteSemitones(key.scale, index + 1) : 12);
        if (degree == size) {
            degree = 0;
            ++octave;
        }
    }
    if (octave < 0 || octave * 12 + degree > 127 || acc < -1 || acc > 1) return false;
    note = static_cast<uint8_t>(octave * 12 + degree);
    accidental = static_cast<int8_t>(acc);
    return true;
}

/// MIDI pitch for a note byte from a pattern cell, read according to `mode`.
constexpr int resolvePitch(PitchMode mode, uint8_t note, int accidental, const PitchContext& ctx) {
    switch (mode) {
    case PitchMode::Absolute:
        return note;
    case PitchMode::Transpose:
        return detail::clampMidi(note + rootShift(ctx.home.root, ctx.current.root) + ctx.transpose);
    case PitchMode::Degree:
        return resolveDegree(note / 12, note % 12, accidental, ctx);
    }
    return note;
}

} // namespace adaptive
