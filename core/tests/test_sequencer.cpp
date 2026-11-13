#include <doctest.h>

#include <adaptive/engine.hpp>

#include <memory>
#include <vector>

using namespace adaptive;

namespace {

constexpr uint32_t kSampleRate = 48000;
// At 120 BPM and 48 kHz: 250 samples per tick, 1000 per effect tick, 24000 per beat.
constexpr uint32_t kSamplesPerTick = 250;

struct Fixture {
    std::unique_ptr<Engine> engine = std::make_unique<Engine>(kSampleRate);
    std::vector<float> buf;
    uint64_t renderedSamples = 0;

    Fixture() { engine->setTempoMilliBpm(120000); }

    /// Render until tick `tick` (counted from the start) has been processed:
    /// rows and effects for that tick have been applied.
    void reachTick(uint64_t tick) {
        const uint64_t target = tick * kSamplesPerTick + 1;
        REQUIRE(target > renderedSamples);
        buf.resize(target - renderedSamples);
        engine->render(buf.data(), static_cast<uint32_t>(buf.size()));
        renderedSamples = target;
    }
    Channel& ch() { return engine->channel(0); }
};

std::unique_ptr<Pattern> makePattern(uint16_t rows, uint8_t rowsPerBeat) {
    auto p = std::make_unique<Pattern>();
    p->numRows = rows;
    p->rowsPerBeat = rowsPerBeat;
    return p;
}

} // namespace

TEST_CASE("Sequence: patterns of different lengths and row rates play in order and loop") {
    Fixture f;
    auto a = makePattern(4, 4);  // one beat = 96 ticks
    auto b = makePattern(3, 1);  // three beats = 288 ticks
    a->cells[0][0] = Cell::on(60);
    b->cells[0][0] = Cell::on(67);
    Sequence seq;
    REQUIRE(seq.append(a.get()));
    REQUIRE(seq.append(b.get()));
    f.ch().playSequence(&seq, 0);

    f.reachTick(0);
    CHECK(f.ch().sequencePosition() == 0);
    CHECK(f.ch().column(0).pitch == 60.0f);
    f.reachTick(95);
    CHECK(f.ch().sequencePosition() == 0);
    f.reachTick(96);  // pattern b starts
    CHECK(f.ch().sequencePosition() == 1);
    CHECK(f.ch().column(0).pitch == 67.0f);
    f.reachTick(383);
    CHECK(f.ch().sequencePosition() == 1);
    f.reachTick(384);  // back to a
    CHECK(f.ch().sequencePosition() == 0);
    CHECK(f.ch().column(0).pitch == 60.0f);
}

TEST_CASE("Sequence: append rejects invalid patterns") {
    Sequence seq;
    auto bad = makePattern(4, 5);  // 5 rows per beat does not divide 96
    CHECK_FALSE(seq.append(bad.get()));
    CHECK_FALSE(seq.append(nullptr));
    CHECK(seq.length == 0);
}

TEST_CASE("Effects: arpeggio cycles base, +x, +y on each effect tick") {
    Fixture f;
    auto p = makePattern(4, 1);
    p->cells[0][0] = Cell::on(60).with(fx::kArpeggio, 0x47);
    f.ch().play(p.get(), 0);

    f.reachTick(0);
    CHECK(f.ch().column(0).soundingPitch() == 60.0f);
    f.reachTick(4);
    CHECK(f.ch().column(0).soundingPitch() == 64.0f);
    f.reachTick(8);
    CHECK(f.ch().column(0).soundingPitch() == 67.0f);
    f.reachTick(12);
    CHECK(f.ch().column(0).soundingPitch() == 60.0f);
}

TEST_CASE("Effects: arpeggio in Degree mode uses scale steps") {
    Fixture f;
    f.ch().setPitchMode(PitchMode::Degree);
    f.engine->pitch().current = Key{0, scales::kNaturalMinor};
    auto p = makePattern(4, 1);
    p->cells[0][0] = Cell::degree(4, 0).with(fx::kArpeggio, 0x24);  // root, 3rd, 5th
    f.ch().play(p.get(), 0);

    f.reachTick(0);
    CHECK(f.ch().column(0).arpOffset[1] == 3);  // minor third
    CHECK(f.ch().column(0).arpOffset[2] == 7);
}

TEST_CASE("Effects: portamento up and down move by xx/16 semitone per effect tick") {
    Fixture f;
    auto p = makePattern(4, 1);  // rows last 96 ticks = 24 effect ticks
    p->cells[0][0] = Cell::on(60).with(fx::kPortaUp, 0x08);
    p->cells[1][0] = Cell::effectOnly(fx::kPortaDown, 0x10);
    f.ch().play(p.get(), 0);

    f.reachTick(95);  // effect ticks 4, 8, ..., 92: 23 steps; none on the row's first tick
    CHECK(f.ch().column(0).pitch == doctest::Approx(60.0f + 23 * 0.5f));
    f.reachTick(96);  // row 1 starts: the new command replaces the slide
    CHECK(f.ch().column(0).pitch == doctest::Approx(71.5f));
    f.reachTick(104);  // ticks 100 and 104
    CHECK(f.ch().column(0).pitch == doctest::Approx(71.5f - 2.0f));
}

TEST_CASE("Effects: slides continue across rows until stopped or a new note") {
    Fixture f;
    auto p = makePattern(5, 1);
    p->cells[0][0] = Cell::on(60).with(fx::kPortaUp, 0x10);  // 1 semitone per effect tick
    p->cells[2][0] = Cell::effectOnly(fx::kPortaUp, 0x00);   // stop
    p->cells[3][0] = Cell::on(50).with(fx::kPortaDown, 0x08);
    p->cells[4][0] = Cell::on(40);                            // a new note ends the slide
    f.ch().play(p.get(), 0);

    f.reachTick(191);  // 23 steps in row 0, then 24 in row 1 (including its first tick)
    CHECK(f.ch().column(0).pitch == doctest::Approx(60.0f + 47.0f));
    f.reachTick(287);
    CHECK(f.ch().column(0).pitch == doctest::Approx(107.0f));
    f.reachTick(383);
    CHECK(f.ch().column(0).pitch == doctest::Approx(50.0f - 23 * 0.5f));
    f.reachTick(400);
    CHECK(f.ch().column(0).pitch == 40.0f);
}

TEST_CASE("Effects: tone portamento slides to the note without retriggering or overshooting") {
    Fixture f;
    auto p = makePattern(4, 1);
    p->cells[0][0] = Cell::on(60);
    p->cells[1][0] = Cell::on(62).with(fx::kTonePorta, 0x05);  // 5/16 per effect tick
    p->cells[2][0] = Cell::on(55).with(fx::kTonePorta, 0x00);  // reuse speed
    f.ch().play(p.get(), 0);

    f.reachTick(96);
    CHECK(f.ch().column(0).pitch == 60.0f);
    CHECK(f.ch().column(0).target == 62.0f);
    f.reachTick(108);
    CHECK(f.ch().column(0).pitch == doctest::Approx(60.0f + 3 * 5 / 16.0f));
    f.reachTick(191);
    CHECK(f.ch().column(0).pitch == 62.0f);  // reached and stopped

    f.reachTick(200);  // row 2 at 192: slide down with remembered speed
    CHECK(f.ch().column(0).pitch == doctest::Approx(62.0f - 2 * 5 / 16.0f));
    // 7 semitones at 5/16 needs 23 ticks: it carries on into row 3, which is empty.
    f.reachTick(300);
    CHECK(f.ch().column(0).pitch == 55.0f);
    CHECK(f.ch().column(0).glide == 0.0f);
}

TEST_CASE("Effects: volume column and volume slide") {
    Fixture f;
    auto p = makePattern(4, 1);
    p->cells[0][0] = Cell::on(60, 64);
    p->cells[1][0] = Cell::effectOnly(fx::kVolumeSlide, 0x02);  // down 2/64 per effect tick
    f.ch().play(p.get(), 0);

    f.reachTick(0);
    CHECK(f.ch().column(0).volume == doctest::Approx(64 / 127.0f));
    f.reachTick(95);
    CHECK(f.ch().column(0).volume == doctest::Approx(64 / 127.0f));  // no slide on row 0
    f.reachTick(136);  // effect ticks 100..136: 10 steps
    CHECK(f.ch().column(0).volume == doctest::Approx(64 / 127.0f - 10 * 2 / 64.0f));
}

TEST_CASE("Effects: synth parameter effects set the voice's baseline") {
    Fixture f;
    auto p = makePattern(1, 1);
    p->cells[0][0] = Cell::on(60).with(fx::kFilterCutoff, 0xFF);
    f.ch().play(p.get(), 0);
    f.reachTick(0);
    CHECK(f.ch().voice(0).getBaseCutoff() == doctest::Approx(20000.0f).epsilon(0.001));

    auto q = makePattern(1, 1);
    q->cells[0][0] = Cell::effectOnly(fx::kWaveShape, 0x80);
    f.ch().play(q.get(), 96);
    f.reachTick(96);
    CHECK(f.ch().voice(0).getWaveformShape() == doctest::Approx(128 / 255.0f));
}

TEST_CASE("Key changes apply to notes that start afterwards") {
    Fixture f;
    f.ch().setPitchMode(PitchMode::Degree);
    auto p = makePattern(2, 1);
    p->cells[0][0] = Cell::degree(4, 2);  // the 3rd
    f.ch().play(p.get(), 0);

    f.reachTick(0);
    CHECK(f.ch().column(0).pitch == 64.0f);  // E in C major
    f.engine->pitch().current = Key{0, scales::kNaturalMinor};
    f.reachTick(191);
    CHECK(f.ch().column(0).pitch == 64.0f);  // the sounding note keeps its pitch
    f.reachTick(192);                         // the pattern loops: the 3rd starts again
    CHECK(f.ch().column(0).pitch == 63.0f);  // E flat
}
