// Editor helpers that run pressense's own oscillator and envelopes.
#include <doctest.h>

#include <adaptive/inspect.hpp>

using namespace adaptive;

TEST_CASE("inspect: waveform comes from the oscillator") {
    float saw[64], square[64];
    inspect::renderWaveform(0.0f, 1.0f, saw, 64);
    inspect::renderWaveform(1.0f, 1.0f, square, 64);
    CHECK(saw[16] == doctest::Approx(-0.5f).epsilon(0.01));  // a quarter of the way up the ramp
    CHECK(square[16] == doctest::Approx(1.0f).epsilon(0.01));
    CHECK(square[48] == doctest::Approx(-1.0f).epsilon(0.01));
}

TEST_CASE("inspect: envelope trace times and levels") {
    const auto t = inspect::traceEnvelope(0.01f, 0.1f, 0.5f, 0.2f);
    CHECK(t.seconds[0] == doctest::Approx(0.01f).epsilon(0.02));
    CHECK(t.seconds[1] == doctest::Approx(0.1f).epsilon(0.02));
    CHECK(t.seconds[2] == doctest::Approx(0.2f).epsilon(0.02));
    CHECK(t.levels[0][0] == 0.0f);
    CHECK(t.levels[0][inspect::EnvelopeTrace::kPoints - 1] == 1.0f);
    CHECK(t.levels[1][0] == 1.0f);
    CHECK(t.levels[1][inspect::EnvelopeTrace::kPoints - 1] == doctest::Approx(0.5f));
    CHECK(t.levels[2][0] == doctest::Approx(0.5f));
    CHECK(t.levels[2][inspect::EnvelopeTrace::kPoints - 1] == 0.0f);
    CHECK_FALSE(t.releaseUnfinished);
    // Monotonic within each stage.
    for (uint32_t k = 1; k < inspect::EnvelopeTrace::kPoints; ++k) {
        CHECK(t.levels[0][k] >= t.levels[0][k - 1]);
        CHECK(t.levels[2][k] <= t.levels[2][k - 1]);
    }
}

TEST_CASE("inspect: zero sustain and very short stages") {
    const auto t = inspect::traceEnvelope(0.0005f, 0.03f, 0.0f, 0.02f);
    CHECK(t.levels[1][inspect::EnvelopeTrace::kPoints - 1] == 0.0f);
    CHECK(t.seconds[2] < 0.001f);  // nothing left to release
    CHECK_FALSE(t.releaseUnfinished);
}
