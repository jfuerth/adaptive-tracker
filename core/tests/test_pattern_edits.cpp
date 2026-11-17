// Pattern and channel shape edits: length, speed, columns, clearing. Made
// both to a song directly (the app's copy) and through the engine while it plays.
#include <doctest.h>

#include <adaptive/engine.hpp>

#include <memory>
#include <vector>

using namespace adaptive;

namespace {

constexpr uint32_t kSamplesPerTick = 250;  // 120 BPM at 48 kHz
constexpr uint32_t kTicksPerRow = 24;      // speed 6

struct Fixture {
    std::unique_ptr<Engine> engine = std::make_unique<Engine>(48000);
    std::unique_ptr<Song> song = std::make_unique<Song>();
    std::vector<float> buf;

    /// Channel 0 plays sequence `entries`; every pattern has 4 rows at speed 6 and 2 columns.
    explicit Fixture(std::initializer_list<uint8_t> entries = {0}) {
        engine->setTempoMilliBpm(120000);
        song->addChannel(PitchMode::Absolute, 2);
        for (int i = 0; i < 2; ++i) {
            Pattern& p = song->patterns[song->addPattern(0, 4, 6)];
            for (uint32_t r = 0; r < 4; ++r) p.cells[r][0] = Cell::on(uint8_t(60 + r));
        }
        for (uint8_t e : entries) song->sections[0].sequences[0].append(e);
        song->numSections = 1;
        song->addInstrument(Instrument());
        Instrument bright;
        bright.set(Instrument::Param::Cutoff, 9000.0f);
        song->addInstrument(bright);
        song->channels[0].instruments[0] = 0;
        song->channels[0].instruments[1] = 1;
        engine->setSong(song.get());
        engine->post(Command::gotoSection(0, Quantize::Now));
    }

    void ticks(uint32_t n) {
        buf.resize(n * kSamplesPerTick);
        engine->render(buf.data(), static_cast<uint32_t>(buf.size()));
    }
    int row() const { return engine->channel(0).playingRow(); }
    /// Renders tick by tick until row `r` starts; returns how many ticks that took.
    uint32_t ticksUntilRow(int r) {
        uint32_t n = 0;
        do {
            ticks(1);
            ++n;
        } while (row() != r && n < 10000);
        return n;
    }
    const Pattern* playing() const { return engine->channel(0).playingPattern(); }
};

} // namespace

TEST_CASE("Pattern edits: a longer pattern keeps playing past its old end") {
    Fixture f;
    f.ticks(2 * kTicksPerRow + 5);
    CHECK(f.row() == 2);
    f.engine->post(Command::setPatternShape(0, 8, 6));
    f.ticks(kTicksPerRow);
    CHECK(f.row() == 3);
    f.ticks(kTicksPerRow);
    CHECK(f.row() == 4);  // would have wrapped to 0 at 4 rows
    f.ticks(4 * kTicksPerRow);
    CHECK(f.row() == 0);  // wraps at 8
}

TEST_CASE("Pattern edits: a slower speed stretches the current row") {
    Fixture f;
    f.ticksUntilRow(2);
    f.ticks(4);  // ticks 0-4 of row 2 played; the edit lands on tick 5
    f.engine->post(Command::setPatternShape(0, 4, 12));
    CHECK(f.ticksUntilRow(3) == 2 * kTicksPerRow - 4);  // ticks 5..47, then row 3
    CHECK(f.ticksUntilRow(0) == 2 * kTicksPerRow);
}

TEST_CASE("Pattern edits: shortening a pattern before the current row moves on") {
    Fixture f;
    f.ticks(3 * kTicksPerRow + 5);
    CHECK(f.row() == 3);
    f.engine->post(Command::setPatternShape(0, 2, 6));
    f.ticks(1);
    CHECK(f.row() == 0);
    f.ticks(2 * kTicksPerRow);
    CHECK(f.row() == 0);  // now loops every 2 rows
}

TEST_CASE("Pattern edits: changing a later entry of a sequence leaves the current one in place") {
    Fixture f({0, 1});
    f.ticks(kTicksPerRow + 5);
    CHECK(f.playing() == &f.song->patterns[0]);
    f.engine->post(Command::setPatternShape(1, 2, 6));
    f.ticks(kTicksPerRow);
    CHECK(f.playing() == &f.song->patterns[0]);
    CHECK(f.row() == 2);
    f.ticks(2 * kTicksPerRow);
    CHECK(f.playing() == &f.song->patterns[1]);
    CHECK(f.row() == 0);
    f.ticks(2 * kTicksPerRow);
    CHECK(f.playing() == &f.song->patterns[0]);  // pattern 1 is 2 rows now
    CHECK(f.row() == 0);
}

TEST_CASE("Pattern edits: speed 1 to 255, 1 to 256 rows") {
    Song song;
    song.addChannel(PitchMode::Absolute, 1);
    song.addPattern(0);
    CHECK(song.setPatternShape(0, 256, 1));
    CHECK(song.setPatternShape(0, 1, 255));
    CHECK_FALSE(song.setPatternShape(0, 0, 6));
    CHECK_FALSE(song.setPatternShape(0, 257, 6));
    CHECK_FALSE(song.setPatternShape(0, 16, 0));
    CHECK_FALSE(song.setPatternShape(1, 16, 6));
}

TEST_CASE("Pattern edits: rows past the end are kept") {
    Fixture f;
    f.song->setPatternShape(0, 2, 6);
    f.song->setPatternShape(0, 4, 6);
    CHECK(f.song->patterns[0].cells[3][0].note == 63);
}

TEST_CASE("Pattern edits: clear a column") {
    Fixture f;
    f.song->patterns[0].cells[1][1] = Cell::on(40);
    f.engine->post(Command::clearColumn(0, 0));
    f.ticks(1);
    for (uint32_t r = 0; r < 4; ++r) CHECK(f.song->patterns[0].cells[r][0].note == Cell::kNoteEmpty);
    CHECK(f.song->patterns[0].cells[1][1].note == 40);
    CHECK(f.song->patterns[1].cells[0][0].note == 60);  // other patterns untouched
}

TEST_CASE("Column edits: removing a column moves later ones left, and adding brings it back") {
    Song song;
    song.addChannel(PitchMode::Absolute, 3);
    song.addChannel(PitchMode::Absolute, 3);
    song.addPattern(0);
    song.addPattern(1);
    for (uint8_t c = 0; c < 3; ++c) {
        song.channels[0].instruments[c] = c;
        song.patterns[0].cells[0][c] = Cell::on(uint8_t(60 + c));
        song.patterns[1].cells[0][c] = Cell::on(uint8_t(70 + c));
    }
    CHECK(song.removeColumn(0, 0));
    CHECK(song.channels[0].numColumns == 2);
    CHECK(song.patterns[0].cells[0][0].note == 61);
    CHECK(song.patterns[0].cells[0][1].note == 62);
    CHECK(song.patterns[0].cells[0][2].note == 60);  // kept past the end
    CHECK(song.channels[0].instruments[0] == 1);
    CHECK(song.channels[0].instruments[2] == 0);
    CHECK(song.patterns[1].cells[0][0].note == 70);  // another channel's pattern

    CHECK(song.setColumns(0, 3));
    CHECK(song.patterns[0].cells[0][2].note == 60);
    CHECK(song.channels[0].instruments[2] == 0);

    CHECK(song.removeColumn(0, 2));
    CHECK(song.removeColumn(0, 1));
    CHECK_FALSE(song.removeColumn(0, 0));  // a channel keeps one column
    CHECK_FALSE(song.setColumns(0, 0));
    CHECK_FALSE(song.setColumns(0, config::kMaxColumns + 1));
}

TEST_CASE("Column edits: an added column with no instrument takes the one before it") {
    Song song;
    song.addChannel(PitchMode::Absolute, 1);
    song.channels[0].instruments[0] = 3;
    CHECK(song.setColumns(0, 3));
    CHECK(song.channels[0].instruments[1] == 3);
    CHECK(song.channels[0].instruments[2] == 3);
}

TEST_CASE("Column edits: the playing channel follows") {
    Fixture f;
    f.song->patterns[0].cells[0][1] = Cell::on(48);
    f.ticks(1);
    CHECK(f.engine->channel(0).voice(1).isActive());

    f.engine->post(Command::setColumns(0, 1));
    f.ticks(1);
    CHECK(f.engine->channel(0).numColumns() == 1);

    f.engine->post(Command::setColumns(0, 3));
    f.ticks(1);
    CHECK(f.engine->channel(0).numColumns() == 3);
    // Column 2 had no instrument, so it took column 1's (the bright one).
    CHECK(f.song->channels[0].instruments[2] == 1);
    CHECK(f.engine->channel(0).voice(2).getBaseCutoff() == doctest::Approx(9000.0f));

    f.engine->post(Command::removeColumn(0, 0));
    f.ticks(1);
    CHECK(f.engine->channel(0).numColumns() == 2);
    CHECK(f.song->patterns[0].cells[0][0].note == 48);
    // Column 0 now has column 1's instrument.
    CHECK(f.engine->channel(0).voice(0).getBaseCutoff() == doctest::Approx(9000.0f));
}

TEST_CASE("Pattern edits: clear part of a column") {
    Fixture f;
    for (uint32_t r = 0; r < 4; ++r) f.song->patterns[0].cells[r][0] = Cell::on(uint8_t(60 + r), 100).with(fx::kSetVolume, 9);
    f.engine->post(Command::clearCells(0, 0, 1, 255, kCellEffect));
    f.ticks(1);
    const Pattern& p = f.song->patterns[0];
    CHECK(p.cells[0][0].effect == fx::kSetVolume);
    for (uint32_t r = 1; r < 4; ++r) {
        CHECK(p.cells[r][0].effect == fx::kNone);
        CHECK(p.cells[r][0].note == 60 + r);
        CHECK(p.cells[r][0].volume == 100);
    }
    CHECK(f.song->clearCells(0, 0, 0, 2, kCellNote | kCellVolume));
    CHECK(p.cells[2][0].note == Cell::kNoteEmpty);
    CHECK(p.cells[2][0].volume == Cell::kVolumeDefault);
    CHECK(p.cells[3][0].note == 63);
    CHECK_FALSE(f.song->clearCells(0, 0, 3, 2, kCellAll));
}

TEST_CASE("Pattern edits: insert and pull up rows") {
    Fixture f;
    Pattern& p = f.song->patterns[0];
    for (uint32_t r = 0; r < 4; ++r) p.cells[r][1] = Cell::on(uint8_t(70 + r));
    p.cells[4][0] = Cell::on(99);  // past the end: stays put

    // Insert at row 1 in column 0, through the engine.
    f.engine->post(Command::shiftRows(0, 0b01, 1, true));
    f.ticks(1);
    CHECK(p.cells[0][0].note == 60);
    CHECK(p.cells[1][0].note == Cell::kNoteEmpty);
    CHECK(p.cells[2][0].note == 61);
    CHECK(p.cells[3][0].note == 62);  // 63 dropped off the end
    CHECK(p.cells[4][0].note == 99);
    for (uint32_t r = 0; r < 4; ++r) CHECK(p.cells[r][1].note == 70 + r);  // other column untouched

    // Pull up at row 0 in both columns.
    CHECK(f.song->shiftRows(0, 0b11, 0, false));
    CHECK(p.cells[0][0].note == Cell::kNoteEmpty);
    CHECK(p.cells[1][0].note == 61);
    CHECK(p.cells[2][0].note == 62);
    CHECK(p.cells[3][0].note == Cell::kNoteEmpty);
    CHECK(p.cells[0][1].note == 71);
    CHECK(p.cells[2][1].note == 73);
    CHECK(p.cells[3][1].note == Cell::kNoteEmpty);
    CHECK(p.cells[4][0].note == 99);

    // The last row: insert and pull just empty it.
    CHECK(f.song->shiftRows(0, 0b10, 3, false));
    CHECK(f.song->shiftRows(0, 0b01, 3, true));
    CHECK_FALSE(f.song->shiftRows(0, 0b01, 4, true));  // past the end
    CHECK_FALSE(f.song->shiftRows(9, 0b01, 0, true));  // no such pattern
}

TEST_CASE("Pattern edits: stretch and squash rows") {
    Song song;
    song.addChannel(PitchMode::Absolute, 2);
    Pattern& p = song.patterns[song.addPattern(0, 4, 6)];
    for (uint32_t r = 0; r < 4; ++r) p.cells[r][1] = Cell::on(uint8_t(60 + r));

    CHECK(song.scaleRows(0, 3, 1));
    CHECK(p.numRows == 12);
    for (uint32_t r = 0; r < 12; ++r) {
        CHECK(p.cells[r][1].note == (r % 3 == 0 ? 60 + r / 3 : Cell::kNoteEmpty));
    }
    CHECK(song.scaleRows(0, 1, 3));
    CHECK(p.numRows == 4);
    for (uint32_t r = 0; r < 4; ++r) CHECK(p.cells[r][1].note == 60 + r);
    CHECK(p.cells[4][1].note == Cell::kNoteEmpty);

    // Squashing drops rows between; an odd length rounds up.
    CHECK(song.setPatternShape(0, 3, 6));
    CHECK(song.scaleRows(0, 1, 2));
    CHECK(p.numRows == 2);
    CHECK(p.cells[0][1].note == 60);
    CHECK(p.cells[1][1].note == 62);
    CHECK(p.cells[3][1].note == Cell::kNoteEmpty);  // hidden row 3 is odd: dropped

    CHECK(song.setPatternShape(0, 100, 6));
    CHECK_FALSE(song.scaleRows(0, 3, 1));  // 300 rows
    CHECK(p.numRows == 100);
    CHECK_FALSE(song.scaleRows(0, 0, 1));
}

TEST_CASE("Pattern edits: stretching the playing pattern keeps its place") {
    Fixture f;
    f.ticks(kTicksPerRow + 5);
    CHECK(f.row() == 1);
    f.engine->post(Command::scaleRows(0, 2, 1));
    f.ticks(1);
    CHECK(f.row() == 2);  // row 1 is row 2 now
    CHECK(f.engine->channel(0).column(0).pitch == doctest::Approx(61.0f));  // not retriggered
    CHECK(f.ticksUntilRow(3) == kTicksPerRow - 5);  // row 2 (was 1) ends on time
    CHECK(f.song->patterns[0].cells[2][0].note == 61);
}
