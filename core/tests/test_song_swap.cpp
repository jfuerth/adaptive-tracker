// Taking things out of a song (patterns, sections, channels), and handing the
// engine a changed copy of its song while it plays (how deletes and undo
// reach the audio thread).
#include <doctest.h>

#include <adaptive/engine.hpp>

#include <memory>
#include <vector>

using namespace adaptive;

namespace {

constexpr uint32_t kSamplesPerTick = 250;  // 120 BPM at 48 kHz
constexpr uint32_t kTicksPerRow = 24;      // speed 6

/// Channels 0-2, one column each. Channel c has patterns 2c and 2c+1 (4 rows,
/// a note on row 0). Section 0 plays {2c, 2c+1} on each channel; section 1
/// plays {2c+1}.
struct Fixture {
    std::unique_ptr<Engine> engine = std::make_unique<Engine>(48000);
    std::unique_ptr<Song> song = std::make_unique<Song>();
    std::vector<std::unique_ptr<SongSwap>> swaps;
    std::vector<float> buf;

    Fixture() {
        engine->setTempoMilliBpm(120000);
        song->addInstrument(Instrument());
        song->addSection();
        song->addSection();
        for (uint8_t c = 0; c < 3; ++c) {
            song->addChannel(PitchMode::Absolute, 1);
            song->assign(c, 0);
            for (int i = 0; i < 2; ++i) {
                Pattern& p = song->patterns[song->addPattern(c, 4, 6)];
                p.cells[0][0] = Cell::on(uint8_t(60 + c * 2 + i));
            }
            song->sections[0].sequences[c].append(uint8_t(2 * c));
            song->sections[0].sequences[c].append(uint8_t(2 * c + 1));
            song->sections[1].sequences[c].append(uint8_t(2 * c + 1));
        }
        engine->setSong(song.get());
        engine->post(Command::gotoSection(0, Quantize::Now));
    }
    void ticks(uint32_t n) {
        buf.resize(n * kSamplesPerTick);
        engine->render(buf.data(), static_cast<uint32_t>(buf.size()));
    }
    /// The engine's song now (the latest swap taken).
    const Song& live() const {
        const uint32_t taken = engine->swapsTaken();
        return taken == 0 ? *song : swaps[taken - 1]->song;
    }
    /// Swap in a copy of the live song changed by `edit`.
    template<typename Fn>
    void swap(Fn&& edit) {
        auto s = std::make_unique<SongSwap>();
        s->song = live();
        edit(s->song, s->renumbering);
        REQUIRE(engine->offer(s.get()));
        swaps.push_back(std::move(s));
    }
    const Pattern* playing(uint32_t ch) const { return engine->channel(ch).playingPattern(); }
    int row(uint32_t ch) const { return engine->channel(ch).playingRow(); }
};

} // namespace

TEST_CASE("Song removal: a pattern leaves sequences and stingers, later ones move down") {
    auto song = std::make_unique<Song>();
    song->addChannel(PitchMode::Absolute, 1);
    for (int i = 0; i < 3; ++i) song->addPattern(0);
    song->addSection();
    for (uint8_t p : {0, 1, 2, 1}) song->sections[0].sequences[0].append(p);
    song->numEvents = 1;
    song->events[0].add(Command::playStinger(0, 1));
    song->events[0].add(Command::playStinger(0, 2));

    Renumbering r;
    REQUIRE(song->removePattern(1, &r));
    CHECK(song->numPatterns == 2);
    const Sequence& s = song->sections[0].sequences[0];
    REQUIRE(s.length == 2);
    CHECK(s.entries[0] == 0);
    CHECK(s.entries[1] == 1);
    REQUIRE(song->events[0].numActions == 1);
    CHECK(song->events[0].actions[0].data == 1);
    CHECK(r.patterns[0] == 0);
    CHECK(r.patterns[1] == kNoIndex);
    CHECK(r.patterns[2] == 1);
    const Renumbering back = r.inverse();
    CHECK(back.patterns[0] == 0);
    CHECK(back.patterns[1] == 2);
    CHECK(r.then(back).patterns[2] == 2);
}

TEST_CASE("Song removal: a section takes the actions that go to it") {
    auto song = std::make_unique<Song>();
    for (int i = 0; i < 3; ++i) song->addSection();
    song->startSection = 2;
    song->numEvents = 1;
    song->events[0].add(Command::gotoSection(1));
    song->events[0].add(Command::gotoSection(2, Quantize::Bar, Entry::Restart, 1));
    REQUIRE(song->removeSection(1));
    CHECK(song->numSections == 2);
    CHECK(song->startSection == 1);
    REQUIRE(song->events[0].numActions == 1);
    CHECK(song->events[0].actions[0].a == 1);
    CHECK(song->events[0].actions[0].b == kNoIndex);  // its bridge is gone: straight there
}

TEST_CASE("Song removal: a channel takes its patterns, layer bits and mappings") {
    Fixture f;
    Song& s = *f.song;
    s.addLayer();
    s.layers[0] = layerChannelBits(0) | layerBit(1, 0) | layerBit(2, 0);
    Mapping m;
    m.target = Mapping::Target::ChannelGain;
    m.index = 1;
    s.addMapping(m);
    m.index = 2;
    s.addMapping(m);

    Renumbering r;
    REQUIRE(s.removeChannel(1, &r));
    CHECK(s.numChannels == 2);
    CHECK(s.numPatterns == 4);
    CHECK(s.patterns[2].channel == 1);  // was channel 2's first pattern
    CHECK(s.layers[0] == (layerChannelBits(0) | layerBit(1, 0)));
    REQUIRE(s.numMappings == 1);
    CHECK(s.mappings[0].index == 1);
    CHECK(s.sections[0].sequences[1].entries[0] == 2);
    CHECK(s.sections[0].sequences[1].entries[1] == 3);
    CHECK(s.sections[0].sequences[2].length == 0);
    CHECK(r.channels[2] == 1);
    CHECK(r.patterns[4] == 2);
    CHECK(r.patterns[2] == kNoIndex);
}

TEST_CASE("Song swap: an unchanged copy plays on in place") {
    Fixture f;
    f.ticks(kTicksPerRow * 5 + 3);  // second entry, row 1
    REQUIRE(f.row(0) == 1);
    f.swap([](Song&, Renumbering&) {});
    f.ticks(1);
    CHECK(f.engine->swapsTaken() == 1);
    CHECK(f.playing(0) == &f.swaps[0]->song.patterns[1]);
    CHECK(f.row(0) == 1);  // no restart
    f.ticks(kTicksPerRow);
    CHECK(f.row(0) == 2);
}

TEST_CASE("Song swap: deleting a channel moves the later ones down, still playing") {
    Fixture f;
    f.ticks(kTicksPerRow * 2 + 3);
    CHECK(f.engine->channel(2).voice(0).isActive());
    f.swap([](Song& s, Renumbering& r) { s.removeChannel(1, &r); });
    f.ticks(1);
    const Song& live = f.live();
    CHECK(f.playing(1) == &live.patterns[2]);  // channel 2's first pattern, now channel 1's
    CHECK(f.row(1) == 2);
    CHECK(f.engine->channel(1).voice(0).isActive());  // its note carries on
    CHECK(f.playing(2) == nullptr);

    // Undo: the inverse renumbering puts it back, and the restored channel joins in.
    const Renumbering forward = f.swaps[0]->renumbering;
    auto undo = std::make_unique<SongSwap>();
    undo->song = *f.song;
    undo->renumbering = forward.inverse();
    REQUIRE(f.engine->offer(undo.get()));
    f.swaps.push_back(std::move(undo));
    f.ticks(1);
    CHECK(f.playing(2) == &f.live().patterns[4]);
    CHECK(f.row(2) == 2);
    CHECK(f.playing(1) == &f.live().patterns[2]);
}

TEST_CASE("Song swap: deleting the playing pattern moves on; deleting the section stops") {
    Fixture f;
    f.ticks(kTicksPerRow * 1 + 3);  // entry 0 (pattern 0), row 1
    f.swap([](Song& s, Renumbering& r) { s.removePattern(0, &r); });  // channel 0 plays {0} (was 1)
    f.ticks(1);
    CHECK(f.playing(0) == &f.live().patterns[0]);

    f.swap([](Song& s, Renumbering& r) { s.removeSection(0, &r); });
    f.ticks(1);
    CHECK(f.engine->currentSection() == kNoIndex);
    CHECK(f.playing(0) == nullptr);
}

TEST_CASE("Song swap: a pending section change follows the renumbering") {
    Fixture f;
    f.ticks(3);
    f.engine->post(Command::gotoSection(1, Quantize::Bar));
    f.ticks(1);
    // Insert a section before it (as undoing a delete would): 1 becomes 2.
    f.swap([](Song& s, Renumbering& r) {
        s.addSection();
        s.sections[2] = s.sections[1];
        s.sections[1] = Section();
        for (auto& m : r.sections) m = kNoIndex;
        r.sections[0] = 0;
        r.sections[1] = 2;
    });
    f.ticks(4 * 96 + 4);  // past the bar
    CHECK(f.engine->currentSection() == 2);
}

TEST_CASE("Song swap: arpeggios convert with a column's pitch mode") {
    auto song = std::make_unique<Song>();
    song->addChannel(PitchMode::Absolute, 1);
    Pattern& p = song->patterns[song->addPattern(0)];
    p.cells[0][0] = Cell::on(60);  // C4
    p.cells[0][0].effect = fx::kArpeggio;
    p.cells[0][0].effectParam = 0x47;  // major triad in semitones
    p.cells[1][0].effect = fx::kArpeggio;
    p.cells[1][0].effectParam = 0x37;  // minor third: between scale notes in C major from C
    REQUIRE(song->setColumnPitch(0, 0, PitchMode::Degree));
    CHECK(p.cells[0][0].effectParam == 0x24);  // two and four scale steps
    CHECK(p.cells[1][0].effectParam == 0x14);  // Eb is nearest D (ties go down)
    REQUIRE(song->setColumnPitch(0, 0, PitchMode::Absolute));
    CHECK(p.cells[0][0].effectParam == 0x47);
    CHECK(p.cells[1][0].effectParam == 0x27);
}
