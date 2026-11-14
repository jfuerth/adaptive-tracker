// Growing a song while it plays: new patterns, channels and sections, and
// sequence edits.
#include <doctest.h>

#include <adaptive/engine.hpp>

#include <memory>
#include <vector>

using namespace adaptive;

namespace {

constexpr uint32_t kSamplesPerTick = 250;  // 120 BPM at 48 kHz
constexpr uint32_t kTicksPerRow = 24;      // speed 6

/// One channel; patterns 0-2 of 4 rows each; section 0 plays {0, 1}.
struct Fixture {
    std::unique_ptr<Engine> engine = std::make_unique<Engine>(48000);
    std::unique_ptr<Song> song = std::make_unique<Song>();
    std::vector<float> buf;

    Fixture() {
        engine->setTempoMilliBpm(120000);
        song->addChannel(PitchMode::Absolute, 1);
        for (int i = 0; i < 3; ++i) {
            Pattern& p = song->patterns[song->addPattern(0, 4, 6)];
            p.cells[0][0] = Cell::on(uint8_t(60 + i));
        }
        song->addSection();
        song->sections[0].sequences[0].append(0);
        song->sections[0].sequences[0].append(1);
        song->addInstrument(Instrument());
        song->assign(0, 0);
        engine->setSong(song.get());
        engine->post(Command::gotoSection(0, Quantize::Now));
    }
    void ticks(uint32_t n) {
        buf.resize(n * kSamplesPerTick);
        engine->render(buf.data(), static_cast<uint32_t>(buf.size()));
    }
    const Pattern* playing(uint32_t ch = 0) const { return engine->channel(ch).playingPattern(); }
    int row(uint32_t ch = 0) const { return engine->channel(ch).playingRow(); }
    const Pattern* pat(uint32_t i) const { return &song->patterns[i]; }
};

} // namespace

TEST_CASE("Song edits: inserting before the playing entry keeps it playing") {
    Fixture f;
    f.ticks(kTicksPerRow * 5 + 3);  // pattern 1, row 1
    CHECK(f.playing() == f.pat(1));
    CHECK(f.row() == 1);
    f.engine->post(Command::editSequence(0, 0, SeqEdit::Insert, 0, 2));  // {2, 0, 1}
    f.ticks(kTicksPerRow);
    CHECK(f.playing() == f.pat(1));
    CHECK(f.row() == 2);
    f.ticks(kTicksPerRow * 2);
    CHECK(f.playing() == f.pat(2));  // wraps to the new first entry
    CHECK(f.row() == 0);
}

TEST_CASE("Song edits: removing the playing entry moves on at once") {
    Fixture f;
    f.ticks(kTicksPerRow * 1 + 3);  // pattern 0, row 1
    f.engine->post(Command::editSequence(0, 0, SeqEdit::Remove, 0));  // {1}
    f.ticks(1);
    CHECK(f.playing() == f.pat(1));
    CHECK(f.row() == 0);
    f.ticks(kTicksPerRow * 4);
    CHECK(f.playing() == f.pat(1));
    CHECK(f.row() == 0);
}

TEST_CASE("Song edits: replacing the playing entry carries on from the row") {
    Fixture f;
    f.ticks(kTicksPerRow * 2 + 3);
    f.engine->post(Command::editSequence(0, 0, SeqEdit::Set, 0, 2));  // {2, 1}
    f.ticks(1);
    CHECK(f.playing() == f.pat(2));
    CHECK(f.row() == 2);
    f.ticks(kTicksPerRow * 2);
    CHECK(f.playing() == f.pat(1));
}

TEST_CASE("Song edits: removing the last entry silences the channel") {
    Fixture f;
    f.ticks(3);
    f.engine->post(Command::editSequence(0, 0, SeqEdit::Remove, 1));
    f.engine->post(Command::editSequence(0, 0, SeqEdit::Remove, 0));
    f.ticks(1);
    CHECK(f.playing() == nullptr);
    CHECK_FALSE(f.song->editSequence(0, 0, SeqEdit::Remove, 0, 0));
}

TEST_CASE("Song edits: a pattern must belong to the channel") {
    Fixture f;
    f.song->patterns[2].channel = 1;
    CHECK_FALSE(f.song->editSequence(0, 0, SeqEdit::Insert, 0, 2));
    CHECK_FALSE(f.song->editSequence(0, 0, SeqEdit::Insert, 3, 0));  // past the end
    CHECK(f.song->editSequence(0, 0, SeqEdit::Insert, 2, 0));         // append
    CHECK(f.song->sections[0].sequences[0].length == 3);
}

TEST_CASE("Song edits: a new channel with a new pattern joins the section in time") {
    Fixture f;
    f.ticks(kTicksPerRow * 5 + 3);  // section is 5 rows in
    // Written past the end, then announced, as the app does.
    ChannelSetup c;
    c.numColumns = 1;
    c.instruments[0] = 0;
    f.song->channels[1] = c;
    f.engine->post(Command::added(Addition::Channel, 1));
    Pattern p;
    p.channel = 1;
    p.numRows = 2;
    p.ticksPerRow = 6;
    f.song->patterns[3] = p;
    f.engine->post(Command::added(Addition::Pattern, 3));
    f.engine->post(Command::editSequence(0, 1, SeqEdit::Insert, 0, 3));
    f.ticks(1);
    CHECK(f.song->numChannels == 2);
    CHECK(f.song->numPatterns == 4);
    CHECK(f.playing(1) == f.pat(3));
    CHECK(f.row(1) == -1);  // mid-row: its row 1 started before it joined
    f.ticks(kTicksPerRow);
    CHECK(f.row(1) == 0);  // 6 rows from the section start: 2-row pattern, row 0
}

TEST_CASE("Song edits: added must name the next slot") {
    Fixture f;
    f.engine->post(Command::added(Addition::Pattern, 7));
    f.engine->post(Command::added(Addition::Section, 1));
    f.ticks(1);
    CHECK(f.song->numPatterns == 3);
    CHECK(f.song->numSections == 2);
}

TEST_CASE("Song edits: section timing") {
    Fixture f;
    f.engine->post(Command::setSection(0, 90.0f, 3, 0));
    f.ticks(1);
    CHECK(f.song->sections[0].tempoMilliBpm == 90000);
    CHECK(f.song->sections[0].beatsPerBar == 3);
    CHECK_FALSE(f.song->setSectionTiming(0, 120000, 0, 0));
    CHECK_FALSE(f.song->setSectionTiming(1, 120000, 4, 0));
}
