// Engine features used by the pattern editor: hex effect codes, the new
// effects, pattern-fired events, live cell edits, and note previews.
#include <doctest.h>

#include <adaptive/engine.hpp>

#include <cmath>
#include <memory>
#include <vector>

using namespace adaptive;

namespace {

constexpr uint32_t kSamplesPerTick = 250;  // 120 BPM at 48 kHz
constexpr uint64_t kBeat = config::kTicksPerBeat;

struct Fixture {
    std::unique_ptr<Engine> engine = std::make_unique<Engine>(48000);
    std::unique_ptr<Song> song = std::make_unique<Song>();
    std::unique_ptr<Pattern> a = std::make_unique<Pattern>();
    std::unique_ptr<Pattern> b = std::make_unique<Pattern>();
    std::unique_ptr<Sequence> seqA = std::make_unique<Sequence>();
    std::unique_ptr<Sequence> seqB = std::make_unique<Sequence>();
    std::vector<float> buf;
    uint64_t renderedSamples = 0;

    Fixture() {
        engine->setTempoMilliBpm(120000);
        for (Pattern* p : {a.get(), b.get()}) {
            p->numRows = 4;
            p->rowsPerBeat = 1;
        }
        a->cells[0][0] = Cell::on(60);
        b->cells[0][0] = Cell::on(72);
        seqA->append(a.get());
        seqB->append(b.get());
        song->sections[0].sequences[0] = seqA.get();
        song->sections[1].sequences[0] = seqB.get();
        song->numSections = 2;
        song->events[0].add(Command::gotoSection(1, Quantize::Now));
        song->numEvents = 1;
        engine->setSong(song.get());
    }

    void reachTick(uint64_t tick) {
        const uint64_t target = tick * kSamplesPerTick + 1;
        REQUIRE(target > renderedSamples);
        buf.resize(target - renderedSamples);
        engine->render(buf.data(), static_cast<uint32_t>(buf.size()));
        renderedSamples = target;
    }
};

} // namespace

static_assert(fx::fromDigit(0) == '0' && fx::fromDigit(11) == 'B' && fx::fromDigit(15) == 'F');
static_assert(fx::toDigit('A') == 10 && fx::toDigit('5') == 5 && fx::toDigit(fx::kNone) == -1);

TEST_CASE("Effects: 4xy sets vibrato, Cxx sets volume") {
    Fixture f;
    f.a->cells[0][0] = Cell::on(60).with(fx::kVibrato, 0x35);
    f.a->cells[1][0] = Cell::effectOnly(fx::kSetVolume, 0x40);
    f.engine->post(Command::gotoSection(0, Quantize::Now));
    f.reachTick(0);
    CHECK(f.engine->channel(0).voice(0).getVibratoRate() == doctest::Approx(2.0f));
    CHECK(f.engine->channel(0).voice(0).getVibratoDepth() == doctest::Approx(0.5f));
    f.reachTick(kBeat);
    CHECK(f.engine->channel(0).column(0).volume == doctest::Approx(0x40 / 127.0f));
}

TEST_CASE("Bxx fires an event on its row; a section it starts plays from that same tick") {
    Fixture f;
    f.a->cells[2][0] = Cell::effectOnly(fx::kFireEvent, 0x00);  // event 0: go to section 1 now
    f.engine->post(Command::gotoSection(0, Quantize::Now));
    f.reachTick(2 * kBeat - 1);
    CHECK(f.engine->currentSection() == 0);
    f.reachTick(2 * kBeat);
    CHECK(f.engine->currentSection() == 1);
    CHECK(f.engine->sectionStartTick() == 2 * kBeat);  // on the grid, not a tick late
    CHECK(f.engine->channel(0).column(0).pitch == 72.0f);  // section 1's first row already played
}

TEST_CASE("Bxx with an unknown event index does nothing") {
    Fixture f;
    f.a->cells[1][0] = Cell::effectOnly(fx::kFireEvent, 0x07);
    f.engine->post(Command::gotoSection(0, Quantize::Now));
    f.reachTick(2 * kBeat);
    CHECK(f.engine->currentSection() == 0);
}

TEST_CASE("SetCell edits a pattern while it plays") {
    Fixture f;
    f.engine->post(Command::gotoSection(0, Quantize::Now));
    f.reachTick(10);
    f.engine->post(Command::setCell(f.a.get(), 1, 0, Cell::on(67)));
    f.reachTick(11);
    CHECK(f.a->cells[1][0].note == 67);
    f.reachTick(kBeat);
    CHECK(f.engine->channel(0).column(0).pitch == 67.0f);
    CHECK(f.engine->channel(0).playingRow() == 1);
    CHECK(f.engine->channel(0).playingPattern() == f.a.get());
}

TEST_CASE("Previews sound with a channel's patch and stop on release") {
    Fixture f;
    f.engine->channel(0).voice(0).getAmpEnvelope().setReleaseTime(0.01f);
    f.engine->post(Command::previewOn(0, 1, 69));
    f.reachTick(20);
    float peak = 0.0f;
    for (float s : f.buf) peak = std::max(peak, std::fabs(s));
    CHECK(peak > 0.01f);

    f.engine->post(Command::previewOff(1));
    f.reachTick(40);  // release tail
    f.reachTick(60);
    peak = 0.0f;
    for (float s : f.buf) peak = std::max(peak, std::fabs(s));
    CHECK(peak < 1e-3f);
}

TEST_CASE("SoloPattern loops one pattern alone; a column mask silences other columns") {
    Fixture f;
    // Channel 1 gets a two-column pattern; channel 0 plays the section.
    auto chords = std::make_unique<Pattern>();
    chords->numRows = 2;
    chords->rowsPerBeat = 1;
    chords->cells[0][0] = Cell::on(60);
    chords->cells[0][1] = Cell::on(64);
    f.engine->channel(1).setNumColumns(2);
    f.engine->post(Command::gotoSection(0, Quantize::Now));
    f.reachTick(10);
    CHECK(f.engine->channel(0).isPlaying());

    f.engine->post(Command::soloPattern(1, chords.get(), 0b10));  // column 1 only
    f.reachTick(11);
    CHECK_FALSE(f.engine->channel(0).isPlaying());  // everything else stopped
    CHECK(f.engine->channel(1).playingPattern() == chords.get());
    CHECK(f.engine->currentSection() == kNoIndex);
    CHECK(f.engine->isPlaying());
    CHECK(f.engine->channel(1).voice(0).isActive());  // column 0 runs silently
    CHECK(f.engine->channel(1).columnMask() == 0b10);

    // It loops: row 0 again after two beats.
    f.reachTick(11 + 2 * kBeat);
    CHECK(f.engine->channel(1).playingRow() == 0);

    // Song playback clears the solo.
    f.engine->post(Command::gotoSection(0, Quantize::Now));
    f.reachTick(12 + 2 * kBeat);
    CHECK(f.engine->channel(1).columnMask() == ~0u);
}

TEST_CASE("Column mask: masked columns contribute no sound") {
    auto rms = [](uint16_t mask) {
        Fixture f;
        auto p = std::make_unique<Pattern>();
        p->numRows = 1;
        p->rowsPerBeat = 1;
        p->cells[0][0] = Cell::on(57);
        f.engine->post(Command::soloPattern(0, p.get(), mask));
        f.reachTick(40);
        double e = 0;
        for (float s : f.buf) e += double(s) * s;
        return e;
    };
    CHECK(rms(0b1) > 0.0);
    CHECK(rms(0b10) == 0.0);  // column 0 has the note, but only column 1 is heard
}
