#include <doctest.h>

#include <adaptive/engine.hpp>
#include <adaptive/tempo_clock.hpp>

#include <cmath>
#include <memory>
#include <vector>

using namespace adaptive;

namespace {

constexpr uint32_t kSampleRate = 48000;

/// Index of the first sample with |x| above a small threshold, or -1.
long firstSoundingSample(const std::vector<float>& buf, size_t from = 0) {
    for (size_t i = from; i < buf.size(); ++i) {
        if (std::fabs(buf[i]) > 1e-4f) return static_cast<long>(i);
    }
    return -1;
}

std::vector<float> renderSamples(Engine& engine, size_t numFrames, uint32_t blockSize = 256) {
    std::vector<float> out(numFrames);
    for (size_t pos = 0; pos < numFrames; pos += blockSize) {
        uint32_t n = static_cast<uint32_t>(std::min<size_t>(blockSize, numFrames - pos));
        engine.render(out.data() + pos, n);
    }
    return out;
}

/// Short, percussive patch so note onsets are easy to find.
void pluckPatch(synth::WavetableSynth& v) {
    v.getAmpEnvelope().setAttackTime(0.0f);
    v.getAmpEnvelope().setDecayTime(0.05f);
    v.getAmpEnvelope().setSustainLevel(0.0f);
    v.getAmpEnvelope().setReleaseTime(0.01f);
    v.setBaseCutoff(4000.0f);
}

} // namespace

TEST_CASE("TempoClock: 120 BPM at 48 kHz gives exactly 250 samples per tick") {
    TempoClock clock(kSampleRate, 120000);
    CHECK(clock.samplesUntilNextTick() == 250);
    CHECK_FALSE(clock.advance(249));
    CHECK(clock.advance(1));
    CHECK(clock.tick() == 1);
}

TEST_CASE("TempoClock: no drift over ten minutes at a tempo with a fractional tick length") {
    // 133 BPM: 48000*60 / (133*96) = 225.56... samples per tick.
    TempoClock clock(kSampleRate, 133000);
    const uint64_t totalSamples = uint64_t(kSampleRate) * 600;
    uint64_t done = 0;
    while (done < totalSamples) {
        uint32_t n = clock.samplesUntilNextTick();
        if (n > totalSamples - done) n = static_cast<uint32_t>(totalSamples - done);
        clock.advance(n);
        done += n;
    }
    // Exact: floor(600 s * 133/60 beats/s * 96 ticks/beat) = 127680 ticks.
    CHECK(clock.tick() == 127680);
}

TEST_CASE("Pattern: speed is effect ticks per row, any nonzero value") {
    Pattern p;
    p.ticksPerRow = 6;
    CHECK(p.isValid());
    CHECK(p.engineTicksPerRow() == 24);
    p.ticksPerRow = 5;
    CHECK(p.isValid());
    CHECK(p.lengthTicks() == 16u * 20u);
    p.ticksPerRow = 0;
    CHECK_FALSE(p.isValid());
}

TEST_CASE("Engine: a note starts exactly on its row") {
    auto engine = std::make_unique<Engine>(kSampleRate);
    engine->setTempoMilliBpm(120000);  // one beat = 24000 samples

    auto pattern = std::make_unique<Pattern>();
    pattern->numRows = 16;

    // The voice is triggered when the engine reaches the row's first sample.
    // (Its audible output starts one sample later: the synth's first sample
    // after a trigger is 0, as the envelope and oscillator start from zero.)
    auto expectTriggerAt = [&](uint32_t sample) {
        engine->channel(0).forEachVoice(pluckPatch);
        engine->channel(0).play(pattern.get(), 0);
        auto before = renderSamples(*engine, sample);
        CHECK_FALSE(engine->channel(0).voice(0).isActive());
        CHECK(firstSoundingSample(before) == -1);
        std::vector<float> one(1);
        engine->render(one.data(), 1);
        CHECK(engine->channel(0).voice(0).isActive());
    };

    SUBCASE("4 rows per beat: row 4 is one beat in") {
        pattern->ticksPerRow = 6;
        pattern->cells[4][0] = Cell::on(69);
        expectTriggerAt(24000);
    }

    SUBCASE("3 rows per beat: row 1 is a triplet eighth in") {
        pattern->ticksPerRow = 8;
        pattern->cells[1][0] = Cell::on(69);
        expectTriggerAt(8000);
    }
}

TEST_CASE("Engine: onsets do not depend on the audio block size") {
    auto pattern = std::make_unique<Pattern>();
    pattern->numRows = 8;
    pattern->ticksPerRow = 6;
    pattern->cells[3][0] = Cell::on(60);

    std::vector<float> reference;
    for (uint32_t block : {1u, 64u, 251u, 4096u}) {
        auto engine = std::make_unique<Engine>(kSampleRate);
        engine->channel(0).forEachVoice(pluckPatch);
        engine->channel(0).play(pattern.get(), 0);
        auto out = renderSamples(*engine, 48000, block);
        if (reference.empty()) {
            reference = out;
        } else {
            CHECK(out == reference);
        }
    }
}

TEST_CASE("Engine: pattern loops at its own length") {
    auto engine = std::make_unique<Engine>(kSampleRate);
    auto pattern = std::make_unique<Pattern>();
    pattern->numRows = 2;     // two sixteenths = 12000 samples at 120 BPM
    pattern->ticksPerRow = 6;
    pattern->cells[0][0] = Cell::on(69);
    engine->channel(0).forEachVoice(pluckPatch);
    engine->channel(0).play(pattern.get(), 0);

    auto out = renderSamples(*engine, 13000);
    // Audible from the sample after the trigger (see "a note starts exactly on its row").
    CHECK(firstSoundingSample(out) == 1);
    // The pluck decays to silence well before the loop comes round again, then
    // sounds again exactly one loop (12000 samples) later.
    CHECK(firstSoundingSample(out, 6000) == 12001);
}

TEST_CASE("Engine: output is bounded and audible with several columns") {
    auto engine = std::make_unique<Engine>(kSampleRate);
    auto pattern = std::make_unique<Pattern>();
    pattern->numRows = 4;
    pattern->ticksPerRow = 24;
    pattern->cells[0][0] = Cell::on(60);
    pattern->cells[0][1] = Cell::on(64);
    pattern->cells[0][2] = Cell::on(67);
    pattern->cells[2][0] = Cell::off();
    pattern->cells[2][1] = Cell::off();
    pattern->cells[2][2] = Cell::off();
    engine->channel(0).setNumColumns(3);
    engine->channel(0).play(pattern.get(), 0);

    auto out = renderSamples(*engine, kSampleRate);
    float peak = 0.0f;
    for (float s : out) {
        REQUIRE(std::isfinite(s));
        peak = std::max(peak, std::fabs(s));
    }
    CHECK(peak > 0.05f);
    CHECK(peak <= 1.0f);
}
