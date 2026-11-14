#include <doctest.h>

#include <adaptive/engine.hpp>
#include <adaptive/spsc_queue.hpp>

#include <memory>
#include <thread>
#include <vector>

using namespace adaptive;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kSamplesPerTick = 250;  // 120 BPM at 48 kHz
constexpr uint64_t kBeat = config::kTicksPerBeat;
constexpr uint64_t kBar = 4 * kBeat;

/// A song with two sections over channels 0 and 1, a bridge, a layer, and a
/// stinger channel (3).
struct Fixture {
    std::unique_ptr<Engine> engine = std::make_unique<Engine>(kSampleRate);
    std::unique_ptr<Song> song = std::make_unique<Song>();
    std::vector<float> buf;
    uint64_t renderedSamples = 0;
    uint8_t stinger = kNoIndex;

    static constexpr uint8_t kExplore = 0, kCombat = 1, kBridge = 2;
    static constexpr uint8_t kStingerChannel = 3;

    Fixture() {
        engine->setTempoMilliBpm(120000);
        // explore: ch0 plays note 60 on every beat of a one-bar pattern; ch1 note 48.
        song->sections[kExplore].sequences[0] = sequenceOf(barPattern(60));
        song->sections[kExplore].sequences[1] = sequenceOf(barPattern(48));
        // combat: ch0 note 72, ch1 not playing, ch2 note 36.
        song->sections[kCombat].sequences[0] = sequenceOf(barPattern(72));
        song->sections[kCombat].sequences[2] = sequenceOf(barPattern(36));
        // bridge: ch0 note 84 for two beats.
        song->sections[kBridge].sequences[0] = sequenceOf(barPattern(84));
        song->sections[kBridge].bridgeBeats = 2;
        song->numSections = 3;

        song->layers[0] = layerChannelBits(1) | layerChannelBits(2);  // the "bass" layer: channels 1 and 2
        song->numLayers = 1;

        stinger = song->addPattern(kStingerChannel, 2, 24);
        song->patterns[stinger].cells[0][0] = Cell::on(90);
        song->patterns[stinger].cells[1][0] = Cell::off();

        song->events[0].add(Command::playStinger(kStingerChannel, stinger));
        song->events[0].add(Command::gotoSection(kCombat, Quantize::Bar));
        song->numEvents = 1;

        engine->setSong(song.get());
    }

    uint8_t barPattern(uint8_t note) {
        const uint8_t i = song->addPattern(0, 4, 24);
        for (int r = 0; r < 4; ++r) song->patterns[i].cells[r][0] = Cell::on(note);
        return i;
    }

    static Sequence sequenceOf(uint8_t pattern) {
        Sequence s;
        s.append(pattern);
        return s;
    }

    /// Render until tick `tick` has been processed.
    void reachTick(uint64_t tick) {
        const uint64_t target = tick * kSamplesPerTick + 1;
        REQUIRE(target > renderedSamples);
        buf.resize(target - renderedSamples);
        engine->render(buf.data(), static_cast<uint32_t>(buf.size()));
        renderedSamples = target;
    }

    float pitch(uint32_t ch) { return engine->channel(ch).column(0).pitch; }
    bool active(uint32_t ch) { return engine->channel(ch).voice(0).isActive(); }
};

} // namespace

TEST_CASE("SpscQueue: FIFO order, capacity, and concurrent use") {
    SpscQueue<int, 4> q;
    CHECK(q.push(1));
    CHECK(q.push(2));
    CHECK(q.push(3));
    CHECK_FALSE(q.push(4));  // holds Capacity - 1
    int v = 0;
    CHECK(q.pop(v));
    CHECK(v == 1);
    CHECK(q.push(4));
    CHECK(q.pop(v));
    CHECK(v == 2);

    SpscQueue<uint32_t, 64> big;
    constexpr uint32_t kCount = 200000;
    std::thread producer([&] {
        for (uint32_t i = 0; i < kCount;) {
            if (big.push(i)) ++i;
        }
    });
    uint32_t expected = 0;
    bool inOrder = true;
    while (expected < kCount) {
        uint32_t x;
        if (big.pop(x)) {
            if (x != expected) inOrder = false;
            ++expected;
        }
    }
    producer.join();
    CHECK(inOrder);
}

TEST_CASE("Sections: start immediately when nothing is playing") {
    Fixture f;
    f.engine->post(Command::gotoSection(Fixture::kExplore, Quantize::Bar));
    f.reachTick(0);
    CHECK(f.engine->currentSection() == Fixture::kExplore);
    CHECK(f.pitch(0) == 60.0f);
    CHECK(f.pitch(1) == 48.0f);
}

TEST_CASE("Sections: a change waits for the next bar, then restarts channels") {
    Fixture f;
    f.engine->post(Command::gotoSection(Fixture::kExplore, Quantize::Now));
    f.reachTick(100);
    f.engine->post(Command::gotoSection(Fixture::kCombat, Quantize::Bar));
    f.reachTick(kBar - 1);
    CHECK(f.engine->currentSection() == Fixture::kExplore);
    f.reachTick(kBar);
    CHECK(f.engine->currentSection() == Fixture::kCombat);
    CHECK(f.engine->sectionStartTick() == kBar);
    CHECK(f.pitch(0) == 72.0f);
    CHECK(f.pitch(2) == 36.0f);
    CHECK_FALSE(f.engine->channel(1).isPlaying());  // not in combat
}

TEST_CASE("Sections: beat quantisation") {
    Fixture f;
    f.engine->post(Command::gotoSection(Fixture::kExplore, Quantize::Now));
    f.reachTick(10);
    f.engine->post(Command::gotoSection(Fixture::kCombat, Quantize::Beat));
    f.reachTick(kBeat);
    CHECK(f.engine->currentSection() == Fixture::kCombat);
}

TEST_CASE("Sections: keep phase continues the bar count and pattern positions") {
    Fixture f;
    f.engine->post(Command::gotoSection(Fixture::kExplore, Quantize::Now));
    f.reachTick(2 * kBeat + 10);  // mid-beat 2 of bar 0
    f.engine->post(Command::gotoSection(Fixture::kCombat, Quantize::Now, Entry::KeepPhase));
    f.reachTick(2 * kBeat + 11);
    CHECK(f.engine->currentSection() == Fixture::kCombat);
    CHECK(f.engine->sectionStartTick() == 0);  // bars still counted from the old start
    // Entered mid-row: the next note comes on the next beat, in time with the old grid.
    CHECK(f.pitch(0) == 60.0f);
    f.reachTick(3 * kBeat);
    CHECK(f.pitch(0) == 72.0f);
}

TEST_CASE("Sections: a bridge plays for its length, then the target restarts") {
    Fixture f;
    f.engine->post(Command::gotoSection(Fixture::kExplore, Quantize::Now));
    f.reachTick(10);
    f.engine->post(Command::gotoSection(Fixture::kCombat, Quantize::Bar, Entry::Restart, Fixture::kBridge));
    f.reachTick(kBar);
    CHECK(f.engine->currentSection() == Fixture::kBridge);
    CHECK(f.pitch(0) == 84.0f);
    f.reachTick(kBar + 2 * kBeat - 1);
    CHECK(f.engine->currentSection() == Fixture::kBridge);
    f.reachTick(kBar + 2 * kBeat);
    CHECK(f.engine->currentSection() == Fixture::kCombat);
    CHECK(f.engine->sectionStartTick() == kBar + 2 * kBeat);
    CHECK(f.pitch(0) == 72.0f);
}

TEST_CASE("Sections: the latest request wins") {
    Fixture f;
    f.engine->post(Command::gotoSection(Fixture::kExplore, Quantize::Now));
    f.reachTick(10);
    f.engine->post(Command::gotoSection(Fixture::kCombat, Quantize::Bar));
    f.reachTick(20);
    f.engine->post(Command::gotoSection(Fixture::kExplore, Quantize::Bar, Entry::KeepPhase));
    f.reachTick(kBar);
    CHECK(f.engine->currentSection() == Fixture::kExplore);
    CHECK(f.pitch(0) == 60.0f);
}

TEST_CASE("Layers: musical mute at the bar, unmute resumes in time") {
    Fixture f;
    f.engine->post(Command::gotoSection(Fixture::kExplore, Quantize::Now));
    f.reachTick(10);
    f.engine->post(Command::setLayerMute(0, true, Quantize::Bar));
    f.reachTick(kBar - 1);
    CHECK_FALSE(f.engine->channel(1).isMuted());
    f.reachTick(kBar);
    CHECK(f.engine->channel(1).isMuted());
    CHECK(f.engine->channel(1).isPlaying());  // keeps its place in the sequence
    CHECK_FALSE(f.engine->channel(0).isMuted());  // not in the layer

    // Muted voices release; after the release tail no note is sounding.
    f.reachTick(kBar + 3 * kBeat);
    CHECK_FALSE(f.active(1));

    f.engine->post(Command::setLayerMute(0, false, Quantize::Beat));
    f.reachTick(kBar + 3 * kBeat + 1);
    CHECK(f.engine->channel(1).isMuted());  // waits for the beat
    f.reachTick(2 * kBar);
    CHECK_FALSE(f.engine->channel(1).isMuted());
    CHECK(f.active(1));
}

TEST_CASE("Events: a stinger plays once on its channel while the section switches at the bar") {
    Fixture f;
    f.engine->post(Command::gotoSection(Fixture::kExplore, Quantize::Now));
    f.reachTick(50);
    f.engine->post(Command::fireEvent(0));
    f.reachTick(51);
    CHECK(f.engine->channel(Fixture::kStingerChannel).isOneShot());
    CHECK(f.pitch(Fixture::kStingerChannel) == 90.0f);
    CHECK(f.engine->currentSection() == Fixture::kExplore);  // section waits for the bar

    // The 2-beat stinger finishes on its own and does not loop...
    f.reachTick(51 + 2 * kBeat - 1);
    CHECK(f.engine->channel(Fixture::kStingerChannel).isOneShot());
    f.reachTick(51 + 2 * kBeat);
    CHECK_FALSE(f.engine->channel(Fixture::kStingerChannel).isPlaying());
    // ...and the section change happens at the bar.
    f.reachTick(kBar);
    CHECK(f.engine->currentSection() == Fixture::kCombat);
}

TEST_CASE("Events: a section change leaves a playing stinger alone") {
    Fixture f;
    f.engine->post(Command::gotoSection(Fixture::kExplore, Quantize::Now));
    f.reachTick(kBar - 10);
    f.engine->post(Command::fireEvent(0));  // stinger now, combat at the bar 10 ticks later
    f.reachTick(kBar);
    CHECK(f.engine->currentSection() == Fixture::kCombat);
    CHECK(f.engine->channel(Fixture::kStingerChannel).isOneShot());
    CHECK(f.pitch(Fixture::kStingerChannel) == 90.0f);
}

TEST_CASE("Stop releases everything") {
    Fixture f;
    f.engine->post(Command::gotoSection(Fixture::kExplore, Quantize::Now));
    f.reachTick(10);
    f.engine->post(Command::stop());
    f.reachTick(11);
    CHECK(f.engine->currentSection() == kNoIndex);
    CHECK_FALSE(f.engine->channel(0).isPlaying());
    CHECK_FALSE(f.engine->channel(1).isPlaying());
}
