#include <doctest.h>

#include <adaptive/engine.hpp>

#include <algorithm>
#include <cmath>
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

/// `ticksPerRow` is the speed: 24 = one row per beat, 6 = four.
std::unique_ptr<Pattern> makePattern(uint16_t rows, uint8_t ticksPerRow) {
    auto p = std::make_unique<Pattern>();
    p->numRows = rows;
    p->ticksPerRow = ticksPerRow;
    return p;
}

} // namespace

TEST_CASE("Sequence: patterns of different lengths and row rates play in order and loop") {
    Fixture f;
    auto pool = std::make_unique<Pattern[]>(2);
    pool[0].numRows = 4;  // speed 6: one beat = 96 ticks
    pool[0].ticksPerRow = 6;
    pool[1].numRows = 3;  // speed 24: three beats = 288 ticks
    pool[1].ticksPerRow = 24;
    pool[0].cells[0][0] = Cell::on(60);
    pool[1].cells[0][0] = Cell::on(67);
    Sequence seq;
    REQUIRE(seq.append(0));
    REQUIRE(seq.append(1));
    f.ch().playSequence(&seq, pool.get(), 0);

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

TEST_CASE("Sequence: a song only plays sequences of valid patterns") {
    auto song = std::make_unique<Song>();
    song->addPattern(0, 4, 6);
    const uint8_t bad = song->addPattern(0, 4, 0);  // speed 0
    Sequence seq;
    REQUIRE(seq.append(0));
    CHECK(song->isPlayable(seq));
    seq.append(bad);
    CHECK_FALSE(song->isPlayable(seq));
    Sequence missing;
    missing.append(7);  // no such pattern
    CHECK_FALSE(song->isPlayable(missing));
    CHECK_FALSE(song->isPlayable(Sequence{}));  // empty
}

TEST_CASE("Speed: rows needn't line up with beats") {
    Fixture f;
    auto p = makePattern(3, 5);  // 5 effect ticks = 20 engine ticks per row
    p->cells[0][0] = Cell::on(60);
    p->cells[1][0] = Cell::on(62);
    p->cells[2][0] = Cell::on(64);
    f.ch().play(p.get(), 0);
    f.reachTick(19);
    CHECK(f.ch().column(0).pitch == 60.0f);
    f.reachTick(20);
    CHECK(f.ch().column(0).pitch == 62.0f);
    f.reachTick(40);
    CHECK(f.ch().column(0).pitch == 64.0f);
    f.reachTick(60);  // loops after 60 ticks
    CHECK(f.ch().column(0).pitch == 60.0f);
}

TEST_CASE("Effects: arpeggio cycles base, +x, +y on each effect tick") {
    Fixture f;
    auto p = makePattern(4, 24);
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
    auto p = makePattern(4, 24);
    p->cells[0][0] = Cell::degree(4, 0).with(fx::kArpeggio, 0x24);  // root, 3rd, 5th
    f.ch().play(p.get(), 0);

    f.reachTick(0);
    CHECK(f.ch().column(0).arpOffset[1] == 3);  // minor third
    CHECK(f.ch().column(0).arpOffset[2] == 7);
}

TEST_CASE("Effects: portamento up and down move by xx/16 semitone per effect tick") {
    Fixture f;
    auto p = makePattern(4, 24);  // rows last 96 ticks = 24 effect ticks
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
    auto p = makePattern(5, 24);
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
    auto p = makePattern(4, 24);
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

TEST_CASE("Effects: a tone portamento note carries on the envelope instead of starting it again") {
    // The New song's Lead: decays from full to half in 250 ms. Four rows per
    // beat, 125 ms (6000 samples) each.
    auto rowLevels = [](bool glide) {
        Fixture f;
        Instrument lead;
        lead.set(Instrument::Param::Shape, 0.5f);
        lead.set(Instrument::Param::Cutoff, 2200.0f);
        lead.set(Instrument::Param::AmpAttack, 0.005f);
        lead.set(Instrument::Param::AmpDecay, 0.25f);
        lead.set(Instrument::Param::AmpSustain, 0.5f);
        lead.set(Instrument::Param::AmpRelease, 0.12f);
        lead.applyTo(f.ch().voice(0));
        auto p = makePattern(4, 6);
        p->cells[0][0] = Cell::on(60);
        p->cells[2][0] = glide ? Cell::on(64).with(fx::kTonePorta, 0x40) : Cell::on(64);
        f.ch().play(p.get(), 0);
        std::vector<float> levels;
        std::vector<float> buf(6000);
        for (int r = 0; r < 4; ++r) {
            f.engine->render(buf.data(), static_cast<uint32_t>(buf.size()));
            double sum = 0.0;
            for (float x : buf) sum += double(x) * x;
            levels.push_back(static_cast<float>(std::sqrt(sum / buf.size())));
        }
        return levels;
    };
    const auto played = rowLevels(false);
    const auto glided = rowLevels(true);
    // The same up to the glide.
    CHECK(glided[0] == doctest::Approx(played[0]));
    CHECK(glided[1] == doctest::Approx(played[1]));
    // A played note attacks again; the glided one stays at the level the
    // first note had decayed to (no new attack, and no release either), so
    // it is quieter than its neighbours when the instrument decays.
    CHECK(played[2] == doctest::Approx(played[0]).epsilon(0.02));
    CHECK(glided[2] < 0.7f * played[2]);
    CHECK(glided[2] == doctest::Approx(glided[3]).epsilon(0.02));  // held at sustain, not dipping further
}

TEST_CASE("Effects: tone portamento holds the level while the pitch moves") {
    Fixture f;
    Instrument flat;  // full sustain, open filter: only the glide could change the level
    flat.set(Instrument::Param::Shape, 0.0f);
    flat.set(Instrument::Param::Cutoff, 20000.0f);
    flat.set(Instrument::Param::AmpSustain, 1.0f);
    flat.set(Instrument::Param::FilterEnvAmount, 0.0f);
    flat.set(Instrument::Param::VibratoDepth, 0.0f);
    flat.applyTo(f.ch().voice(0));
    auto p = makePattern(16, 24);
    p->cells[0][0] = Cell::on(48);
    p->cells[1][0] = Cell::on(84).with(fx::kTonePorta, 0x02);  // three octaves over about six seconds
    f.ch().play(p.get(), 0);
    // 100 ms windows: whole cycles enough that the waveform doesn't show.
    std::vector<float> buf(4800);
    float lowest = 1.0f, highest = 0.0f;
    for (int w = 0; w < 64; ++w) {
        f.engine->render(buf.data(), static_cast<uint32_t>(buf.size()));
        double sum = 0.0;
        for (float x : buf) sum += double(x) * x;
        const float rms = static_cast<float>(std::sqrt(sum / buf.size()));
        if (w >= 6) {  // the glide starts at 500 ms
            lowest = std::min(lowest, rms);
            highest = std::max(highest, rms);
        }
    }
    CHECK(f.ch().column(0).pitch > 80.0f);  // most of the way there
    CHECK(20.0f * std::log10(highest / lowest) < 0.5f);
}

TEST_CASE("Effects: volume column and volume slide") {
    Fixture f;
    auto p = makePattern(4, 24);
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
    auto p = makePattern(1, 24);
    p->cells[0][0] = Cell::on(60).with(fx::kFilterCutoff, 0xFF);
    f.ch().play(p.get(), 0);
    f.reachTick(0);
    CHECK(f.ch().voice(0).getBaseCutoff() == doctest::Approx(20000.0f).epsilon(0.001));

    auto q = makePattern(1, 24);
    q->cells[0][0] = Cell::effectOnly(fx::kWaveShape, 0x80);
    f.ch().play(q.get(), 96);
    f.reachTick(96);
    CHECK(f.ch().voice(0).getWaveformShape() == doctest::Approx(128 / 255.0f));
}

TEST_CASE("Key changes apply to notes that start afterwards") {
    Fixture f;
    f.ch().setPitchMode(PitchMode::Degree);
    auto p = makePattern(2, 24);
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
