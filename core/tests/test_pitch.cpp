#include <doctest.h>

#include <adaptive/pitch.hpp>

using namespace adaptive;

namespace {
PitchContext inKey(uint8_t root, uint16_t scale, DegreeMapping mapping = DegreeMapping::Wrap) {
    PitchContext ctx;
    ctx.home = Key{0, scales::kMajor};  // songs below are written in C major
    ctx.current = Key{root, scale};
    ctx.mapping = mapping;
    return ctx;
}
constexpr uint8_t deg(int octave, int degree) { return static_cast<uint8_t>(octave * 12 + degree); }
} // namespace

// Compile-time checks: the pitch maths is constexpr, so MCU builds can fold it.
static_assert(scaleSize(scales::kMajor) == 7);
static_assert(scaleSize(scales::kMajorPentatonic) == 5);
static_assert(scaleNoteSemitones(scales::kNaturalMinor, 2) == 3);
static_assert(rootShift(0, 2) == 2);
static_assert(rootShift(0, 9) == -3);   // C to A goes down a minor third, not up a sixth
static_assert(rootShift(11, 0) == 1);   // B to C goes up a semitone
static_assert(rootShift(0, 6) == 6);

TEST_CASE("Degree mode in the home key") {
    const auto ctx = inKey(0, scales::kMajor);
    CHECK(resolvePitch(PitchMode::Degree, deg(4, 0), 0, ctx) == 60);  // C4
    CHECK(resolvePitch(PitchMode::Degree, deg(4, 2), 0, ctx) == 64);  // E4
    CHECK(resolvePitch(PitchMode::Degree, deg(4, 6), 0, ctx) == 71);  // B4
    CHECK(resolvePitch(PitchMode::Degree, deg(4, 7), 0, ctx) == 72);  // past the end: next octave's root
    CHECK(resolvePitch(PitchMode::Degree, deg(4, 3), +1, ctx) == 66); // #4 = F#
}

TEST_CASE("Degree mode follows changes of mode and root") {
    SUBCASE("C minor flattens the 3rd, 6th and 7th") {
        const auto ctx = inKey(0, scales::kNaturalMinor);
        CHECK(resolvePitch(PitchMode::Degree, deg(4, 2), 0, ctx) == 63);
        CHECK(resolvePitch(PitchMode::Degree, deg(4, 5), 0, ctx) == 68);
        CHECK(resolvePitch(PitchMode::Degree, deg(4, 6), 0, ctx) == 70);
        CHECK(resolvePitch(PitchMode::Degree, deg(4, 4), 0, ctx) == 67);  // 5th unchanged
    }
    SUBCASE("D dorian: same notes as C major, starting on D") {
        const auto ctx = inKey(2, scales::kDorian);
        CHECK(resolvePitch(PitchMode::Degree, deg(4, 0), 0, ctx) == 62);
        CHECK(resolvePitch(PitchMode::Degree, deg(4, 2), 0, ctx) == 65);
    }
    SUBCASE("A minor is reached by moving down, not up") {
        const auto ctx = inKey(9, scales::kNaturalMinor);
        CHECK(resolvePitch(PitchMode::Degree, deg(4, 0), 0, ctx) == 57);
    }
    SUBCASE("Accidentals stay relative to the scale") {
        const auto ctx = inKey(0, scales::kNaturalMinor);
        CHECK(resolvePitch(PitchMode::Degree, deg(4, 2), +1, ctx) == 64);  // #3 of minor = E
    }
}

TEST_CASE("Degree mode: changing to a scale of a different size") {
    SUBCASE("Wrap: degree n is the n-th pentatonic note") {
        const auto ctx = inKey(0, scales::kMajorPentatonic, DegreeMapping::Wrap);
        CHECK(resolvePitch(PitchMode::Degree, deg(4, 3), 0, ctx) == 67);  // 4th pentatonic note: G
        CHECK(resolvePitch(PitchMode::Degree, deg(4, 5), 0, ctx) == 72);  // wraps to next C
        CHECK(resolvePitch(PitchMode::Degree, deg(4, 6), 0, ctx) == 74);  // ...then D
    }
    SUBCASE("Snap: the home-scale note moves to the nearest pentatonic note") {
        const auto ctx = inKey(0, scales::kMajorPentatonic, DegreeMapping::Snap);
        CHECK(resolvePitch(PitchMode::Degree, deg(4, 3), 0, ctx) == 64);  // F -> E
        CHECK(resolvePitch(PitchMode::Degree, deg(4, 5), 0, ctx) == 69);  // A stays A
        CHECK(resolvePitch(PitchMode::Degree, deg(4, 6), 0, ctx) == 72);  // B -> C above
    }
    SUBCASE("Snap with equal-size scales maps degree for degree") {
        const auto ctx = inKey(0, scales::kNaturalMinor, DegreeMapping::Snap);
        CHECK(resolvePitch(PitchMode::Degree, deg(4, 2), 0, ctx) == 63);
    }
}

TEST_CASE("Absolute and Transpose modes") {
    auto ctx = inKey(2, scales::kNaturalMinor);
    ctx.transpose = 12;
    CHECK(resolvePitch(PitchMode::Absolute, 60, 0, ctx) == 60);       // never moves
    CHECK(resolvePitch(PitchMode::Transpose, 64, 0, ctx) == 64 + 2 + 12);  // root and transpose, not scale
}

TEST_CASE("Pitches are clamped to the MIDI range") {
    auto ctx = inKey(0, scales::kMajor);
    ctx.transpose = 100;
    CHECK(resolvePitch(PitchMode::Degree, deg(9, 6), 0, ctx) == 127);
    ctx.transpose = -100;
    CHECK(resolvePitch(PitchMode::Transpose, 1, 0, ctx) == 0);
}
