// Column settings: pitch mode per column, gates on parameters, and layers
// made of columns.
#include <doctest.h>

#include <adaptive/engine.hpp>
#include <adaptive/songfile.hpp>

#include <memory>
#include <string>
#include <vector>

using namespace adaptive;

namespace {

constexpr uint32_t kSamplesPerTick = 250;  // 120 BPM at 48 kHz
constexpr uint32_t kTicksPerRow = 24;      // speed 6

/// Channel 0 with 2 columns, each with a note on every row of a 4-row pattern;
/// one parameter ("intensity", starting at 0.2).
struct Fixture {
    std::unique_ptr<Engine> engine = std::make_unique<Engine>(48000);
    std::unique_ptr<Song> song = std::make_unique<Song>();
    std::vector<float> buf;

    Fixture() {
        engine->setTempoMilliBpm(120000);
        song->addChannel(PitchMode::Absolute, 2);
        Pattern& p = song->patterns[song->addPattern(0, 4, 6)];
        for (uint32_t r = 0; r < 4; ++r) {
            p.cells[r][0] = Cell::on(60);
            p.cells[r][1] = Cell::on(64);
        }
        song->addSection();
        song->sections[0].sequences[0].append(0);
        song->addInstrument(Instrument());
        song->assign(0, 0);
        song->numParams = 1;
        song->paramInitial[0] = 0.2f;
        song->addLayer();
        song->layers[0] = layerBit(0, 1);  // just column 1
    }
    void start() {
        engine->setSong(song.get());
        engine->post(Command::gotoSection(0, Quantize::Now));
    }
    void ticks(uint32_t n) {
        buf.resize(n * kSamplesPerTick);
        engine->render(buf.data(), static_cast<uint32_t>(buf.size()));
    }
    bool sounding(uint32_t column) { return engine->channel(0).voice(column).isActive(); }
};

} // namespace

TEST_CASE("Columns: degrees convert to and from MIDI notes in the home key") {
    Key cMajor{0, scales::kMajor};
    uint8_t note = 0;
    int8_t acc = 0;
    CHECK(encodeDegree(60, cMajor, note, acc));
    CHECK(note == 4 * 12 + 0);
    CHECK(acc == 0);
    CHECK(encodeDegree(64, cMajor, note, acc));  // E: degree 2
    CHECK(note == 4 * 12 + 2);
    CHECK(encodeDegree(61, cMajor, note, acc));  // C#: degree 0, sharp
    CHECK(note == 4 * 12 + 0);
    CHECK(acc == 1);
    CHECK(encodeDegree(71, cMajor, note, acc));  // B: degree 6
    CHECK(note == 4 * 12 + 6);
    // A pentatonic gap of three semitones: the note above is flattened.
    Key cPenta{0, scales::kMajorPentatonic};  // C D E G A
    CHECK(encodeDegree(70, cPenta, note, acc));  // Bb: A sharp
    CHECK(note == 4 * 12 + 4);
    CHECK(acc == 1);
    CHECK(encodeDegree(71, cPenta, note, acc));  // B: C5 flat
    CHECK(note == 5 * 12 + 0);
    CHECK(acc == -1);
    PitchContext ctx;
    ctx.home = ctx.current = cPenta;
    for (int m = 24; m < 120; ++m) {
        INFO(m);
        REQUIRE(encodeDegree(m, cPenta, note, acc));
        CHECK(resolveDegree(note / 12, note % 12, acc, ctx) == m);
        REQUIRE(encodeDegree(m, Key{7, scales::kHarmonicMinor}, note, acc));
        PitchContext g;
        g.home = g.current = Key{7, scales::kHarmonicMinor};
        CHECK(resolveDegree(note / 12, note % 12, acc, g) == m);
    }
}

TEST_CASE("Columns: changing a column's pitch mode keeps its notes sounding the same") {
    Fixture f;
    f.song->homeKey = Key{2, scales::kNaturalMinor};  // D minor
    f.song->patterns[0].cells[0][1] = Cell::on(65);   // F
    CHECK(f.song->setColumnPitch(0, 1, PitchMode::Degree));
    CHECK(f.song->channels[0].pitchModes[1] == PitchMode::Degree);
    CHECK(f.song->channels[0].pitchModes[0] == PitchMode::Absolute);
    const Cell& c = f.song->patterns[0].cells[0][1];
    CHECK(c.note == 4 * 12 + 2);  // D minor: D E F -> degree 2
    CHECK(f.song->patterns[0].cells[0][0].note == 60);  // other column untouched
    CHECK(f.song->setColumnPitch(0, 1, PitchMode::Transpose));
    CHECK(f.song->patterns[0].cells[0][1].note == 65);
    CHECK(f.song->setColumnPitch(0, 1, PitchMode::Absolute));
    CHECK(f.song->patterns[0].cells[0][1].note == 65);  // fixed and key-following notes are the same numbers
}

TEST_CASE("Columns: the engine plays each column in its own mode") {
    Fixture f;
    f.song->setColumnPitch(0, 1, PitchMode::Degree);
    f.start();
    f.engine->post(Command::setKey(5, scales::kMajor, Quantize::Now));  // F major
    f.ticks(kTicksPerRow + 1);
    // Column 0 is fixed (60); column 1 is degree 2 (E in C) -> A in F major.
    CHECK(f.engine->channel(0).column(0).pitch == doctest::Approx(60.0f));
    CHECK(f.engine->channel(0).column(1).pitch == doctest::Approx(69.0f));
}

TEST_CASE("Columns: a gated column starts no notes below its threshold") {
    Fixture f;
    f.song->setColumnGate(0, 1, 0, 0.5f);
    f.start();
    f.ticks(2);
    CHECK(f.sounding(0));
    CHECK_FALSE(f.sounding(1));  // intensity 0.2 < 0.5

    f.engine->post(Command::setParam(0, 0.6f));
    f.ticks(kTicksPerRow);
    CHECK(f.sounding(1));  // came in with its next note

    f.engine->post(Command::setParam(0, 0.1f));
    f.ticks(1);
    CHECK(f.sounding(1));  // a sounding note isn't cut off
}

TEST_CASE("Columns: hearing everything ignores gates and layer mutes") {
    Fixture f;
    f.song->setColumnGate(0, 1, 0, 0.9f);
    f.engine->setSong(f.song.get());
    f.engine->post(Command::setLayerMute(0, true));
    f.engine->post(Command{Command::Kind::SoloPattern, Quantize::Now, Entry::Restart, 0, 0, 0.0f, 0xFFFF});
    f.ticks(2);
    CHECK_FALSE(f.sounding(1));  // a pattern alone still gets the mix by default

    f.engine->post(Command::hearAll(true));
    f.engine->post(Command{Command::Kind::SoloPattern, Quantize::Now, Entry::Restart, 0, 0, 0.0f, 0xFFFF});
    f.ticks(2);
    CHECK(f.sounding(1));
    CHECK(f.engine->channel(0).mutedColumns() == 0);

    f.engine->post(Command::hearAll(false));
    f.ticks(1);
    CHECK(f.engine->channel(0).mutedColumns() != 0);
}

TEST_CASE("Columns: a layer can hold one column of a channel") {
    Fixture f;
    f.start();
    f.ticks(2);
    CHECK(f.sounding(1));
    f.engine->post(Command::setLayerMute(0, true, Quantize::Now));
    f.ticks(kTicksPerRow);
    CHECK(f.engine->channel(0).mutedColumns() == 0b10);
    CHECK(f.sounding(0));

    f.engine->post(Command::setLayerColumns(0, 0, 0b01));  // now column 0 instead
    f.ticks(1);
    CHECK(f.engine->channel(0).mutedColumns() == 0b01);
}

TEST_CASE("Columns: removing a layer takes what refers to it") {
    Fixture f;
    f.song->addLayer();
    f.song->layers[1] = layerBit(0, 0);
    Mapping m;
    m.target = Mapping::Target::LayerGain;
    m.index = 0;
    f.song->addMapping(m);
    m.index = 1;
    f.song->addMapping(m);
    f.song->events[0].add(Command::setLayerMute(0, true));
    f.song->events[0].add(Command::setLayerMute(1, true));
    f.song->numEvents = 1;

    CHECK(f.song->removeLayer(0));
    CHECK(f.song->numLayers == 1);
    CHECK(f.song->layers[0] == layerBit(0, 0));
    REQUIRE(f.song->numMappings == 1);
    CHECK(f.song->mappings[0].index == 0);
    REQUIRE(f.song->events[0].numActions == 1);
    CHECK(f.song->events[0].actions[0].a == 0);
}

TEST_CASE("Columns: settings survive a song file") {
    Fixture f;
    songfile::SongInfo info;
    info.channels = {{"ch", "Ch"}};
    info.patterns = {{"p", "P"}};
    info.sections = {{"s", "S"}};
    info.params = {{"intensity", "Intensity"}};
    info.layers = {{"l", "L"}};
    info.instruments = {{"i", "I"}};
    f.song->instrumentPitch[0] = uint8_t(PitchMode::Degree);
    f.song->setColumnGate(0, 1, 0, 0.5f);

    f.song->layers[0] = layerBit(0, 1);
    f.song->setColumnPitch(0, 1, PitchMode::Degree);
    const std::string json = songfile::save(*f.song, info);

    auto back = std::make_unique<Song>();
    songfile::SongInfo backInfo;
    const auto r = songfile::load(json, *back, backInfo);
    INFO(r.error);
    REQUIRE(r.ok);
    CHECK(back->channels[0].pitchModes[0] == PitchMode::Absolute);
    CHECK(back->channels[0].pitchModes[1] == PitchMode::Degree);
    CHECK(back->channels[0].gateParams[1] == 0);
    CHECK(back->channels[0].gateThresholds[1] == doctest::Approx(0.5f));
    CHECK(back->channels[0].gateParams[0] == kNoIndex);
    CHECK(back->layers[0] == layerBit(0, 1));
    CHECK(back->instrumentPitch[0] == uint8_t(PitchMode::Degree));
    CHECK(back->patterns[0].cells[0][1].note == f.song->patterns[0].cells[0][1].note);
    CHECK(songfile::save(*back, backInfo) == json);
}
