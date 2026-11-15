// Mixer strip controls: fader, mute, solo, pause and restart.
#include <doctest.h>

#include <adaptive/engine.hpp>

#include <memory>
#include <vector>

using namespace adaptive;

namespace {

constexpr uint32_t kSamplesPerTick = 250;  // 120 BPM at 48 kHz
constexpr uint32_t kTicksPerRow = 24;      // speed 6

/// Two channels, each looping a 4-row pattern with a note on every row.
struct Fixture {
    std::unique_ptr<Engine> engine = std::make_unique<Engine>(48000);
    std::unique_ptr<Song> song = std::make_unique<Song>();
    std::vector<float> buf;

    Fixture() {
        engine->setTempoMilliBpm(120000);
        song->addInstrument(Instrument());
        song->addSection();
        for (uint8_t c = 0; c < 2; ++c) {
            song->addChannel(PitchMode::Absolute, 1);
            song->assign(c, 0);
            Pattern& p = song->patterns[song->addPattern(c, 4, 6)];
            for (uint32_t r = 0; r < 4; ++r) p.cells[r][0] = Cell::on(uint8_t(60 + c));
            song->sections[0].sequences[c].append(c);
        }
        engine->setSong(song.get());
        engine->post(Command::gotoSection(0, Quantize::Now));
    }
    void ticks(uint32_t n) {
        buf.resize(n * kSamplesPerTick);
        engine->render(buf.data(), static_cast<uint32_t>(buf.size()));
    }
    Channel& ch(uint32_t c) { return engine->channel(c); }
};

} // namespace

TEST_CASE("Mixer: the fader and mute set a channel's mix gain, gliding") {
    Fixture f;
    f.ticks(2);
    CHECK(f.ch(0).mixGain() == doctest::Approx(1.0f));
    f.engine->post(Command::setChannelVolume(0, 0.5f));
    f.ticks(2);
    CHECK(f.ch(0).mixGain() == doctest::Approx(0.25f));  // the fader's curve
    CHECK(f.song->channels[0].volume == doctest::Approx(0.5f));

    f.engine->post(Command::setChannelMute(0, true));
    f.ticks(2);
    CHECK(f.ch(0).mixGain() == doctest::Approx(0.0f));
    CHECK(f.ch(0).voice(0).isActive());  // still playing, silently
    f.engine->post(Command::setChannelMute(0, false));
    f.ticks(2);
    CHECK(f.ch(0).mixGain() == doctest::Approx(0.25f));
}

TEST_CASE("Mixer: solo silences the others") {
    Fixture f;
    f.engine->post(Command::setChannelSolo(1, true));
    f.ticks(2);
    CHECK(f.ch(0).mixGain() == doctest::Approx(0.0f));
    CHECK(f.ch(1).mixGain() == doctest::Approx(1.0f));
    f.engine->post(Command::setChannelSolo(1, false));
    f.ticks(2);
    CHECK(f.ch(0).mixGain() == doctest::Approx(1.0f));
}

TEST_CASE("Mixer: a paused channel keeps its place and starts no notes") {
    Fixture f;
    f.ticks(kTicksPerRow + 2);
    f.engine->post(Command::setChannelPause(0, true));
    f.ticks(1);
    CHECK(f.ch(0).paused());
    f.ticks(kTicksPerRow * 4);  // past several notes
    CHECK_FALSE(f.ch(0).voice(0).isActive());
    CHECK(f.ch(0).playingRow() == f.ch(1).playingRow());  // in step with the other channel
    f.engine->post(Command::setChannelPause(0, false));
    f.ticks(kTicksPerRow);
    CHECK(f.ch(0).voice(0).isActive());
}

TEST_CASE("Mixer: restart waits for the bar, then plays from the top") {
    Fixture f;
    f.ticks(kTicksPerRow * 2 + 3);
    f.engine->post(Command::restartChannel(0));  // at the next bar (384 ticks)
    f.ticks(kTicksPerRow);
    CHECK(f.ch(0).playingRow() == 3);
    f.ticks(384 - kTicksPerRow * 3 - 3 + 1);
    CHECK(f.ch(0).playingRow() == 0);
}

TEST_CASE("Mixer: mute and solo move with their channel when one is deleted") {
    Fixture f;
    f.engine->post(Command::setChannelMute(1, true));
    f.ticks(2);
    auto swap = std::make_unique<SongSwap>();
    swap->song = *f.song;
    swap->song.removeChannel(0, &swap->renumbering);
    REQUIRE(f.engine->offer(swap.get()));
    f.ticks(2);
    CHECK(f.ch(0).stripMuted());
    CHECK(f.ch(0).mixGain() == doctest::Approx(0.0f));
    CHECK_FALSE(f.ch(1).stripMuted());
}

TEST_CASE("Rewind: to the bar replays it from its first row, and the bar count starts over") {
    Fixture f;
    f.ticks(384 + kTicksPerRow * 2 + 3);  // bar 2, row 2 of the second pass
    REQUIRE(f.ch(0).playingRow() == 2);
    f.engine->post(Command::rewind(RewindTo::Bar));
    f.ticks(1);
    CHECK(f.ch(0).playingRow() == 0);
    CHECK(f.ch(1).playingRow() == 0);
    CHECK(f.engine->sectionStartTick() == kTicksPerRow * 2 + 3);
    f.ticks(kTicksPerRow);
    CHECK(f.ch(0).playingRow() == 1);
}

TEST_CASE("Rewind: to the pattern starts each channel's pattern again") {
    Fixture f;
    f.ticks(kTicksPerRow * 2 + 3);
    f.engine->post(Command::rewind(RewindTo::Pattern));
    f.ticks(1);
    CHECK(f.ch(0).playingRow() == 0);
    CHECK(f.ch(0).voice(0).isActive());
    f.ticks(kTicksPerRow * 4);  // round the 4-row pattern once more
    CHECK(f.ch(0).playingRow() == 0);
    f.ticks(kTicksPerRow);
    CHECK(f.ch(0).playingRow() == 1);
}
