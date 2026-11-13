#include <doctest.h>

#include <adaptive/engine.hpp>
#include <adaptive/ramp.hpp>

#include <cmath>
#include <memory>
#include <vector>

using namespace adaptive;

namespace {

constexpr uint32_t kSampleRate = 48000;

struct Fixture {
    std::unique_ptr<Engine> engine = std::make_unique<Engine>(kSampleRate);
    std::unique_ptr<Song> song = std::make_unique<Song>();
    std::unique_ptr<Pattern> drone = std::make_unique<Pattern>();
    std::unique_ptr<Sequence> seq = std::make_unique<Sequence>();
    std::vector<float> buf;
    uint64_t renderedSamples = 0;

    static constexpr uint8_t kIntensity = 0;
    static constexpr uint8_t kDanger = 1;

    Fixture() {
        engine->setTempoMilliBpm(120000);
        drone->numRows = 1;
        drone->rowsPerBeat = 1;
        drone->cells[0][0] = Cell::on(57);
        seq->append(drone.get());
        song->sections[0].sequences[0] = seq.get();
        song->sections[0].sequences[1] = seq.get();
        song->sections[1].sequences[0] = seq.get();
        song->numSections = 2;
        song->layers[0] = 1u << 1;  // layer 0 = channel 1
        song->numLayers = 1;
        song->events[0].add(Command::gotoSection(1, Quantize::Now));
        song->events[1].add(Command::gotoSection(0, Quantize::Now));
        song->numEvents = 2;
    }

    void start() {
        engine->setSong(song.get());
        engine->post(Command::gotoSection(0, Quantize::Now));
    }

    void renderSamples(uint32_t n) {
        buf.resize(n);
        engine->render(buf.data(), n);
        renderedSamples += n;
    }
};

} // namespace

TEST_CASE("Ramp: glides linearly and lands exactly") {
    Ramp r(0.0f);
    r.set(1.0f, 4);
    CHECK(r.next() == doctest::Approx(0.25f));
    CHECK(r.next() == doctest::Approx(0.5f));
    r.advance(10);
    CHECK(r.value() == 1.0f);
    CHECK_FALSE(r.isMoving());
    r.set(0.0f, 0);
    CHECK(r.value() == 0.0f);
}

TEST_CASE("Mapping curves") {
    Mapping lin;
    lin.outMin = 0.2f;
    lin.outMax = 1.0f;
    CHECK(lin.map(0.5f) == doctest::Approx(0.6f));
    CHECK(lin.map(2.0f) == doctest::Approx(1.0f));  // parameter clamped

    Mapping hz;
    hz.curve = Mapping::Curve::Exponential;
    hz.outMin = 200.0f;
    hz.outMax = 20000.0f;
    CHECK(hz.map(0.5f) == doctest::Approx(2000.0f));  // geometric midpoint
}

TEST_CASE("Mappings: layer gain fades over the ramp time") {
    Fixture f;
    Mapping m;
    m.param = Fixture::kIntensity;
    m.target = Mapping::Target::LayerGain;
    m.index = 0;
    m.outMin = 0.0f;
    m.outMax = 1.0f;
    m.rampMs = 100;  // 4800 samples
    f.song->addMapping(m);
    f.start();

    f.renderSamples(256);
    CHECK(f.engine->channel(1).currentGain() == 0.0f);  // intensity 0 -> silent layer
    CHECK(f.engine->channel(0).currentGain() == 1.0f);  // not in the layer

    f.engine->post(Command::setParam(Fixture::kIntensity, 1.0f));
    f.renderSamples(2400 + 250);  // the command lands on the next tick
    const float mid = f.engine->channel(1).currentGain();
    CHECK(mid > 0.3f);
    CHECK(mid < 0.7f);
    f.renderSamples(4800);
    CHECK(f.engine->channel(1).currentGain() == 1.0f);
}

TEST_CASE("Mappings: channel cutoff and master cutoff follow a parameter") {
    Fixture f;
    Mapping ch;
    ch.param = Fixture::kDanger;
    ch.target = Mapping::Target::ChannelCutoff;
    ch.index = 0;
    ch.curve = Mapping::Curve::Exponential;
    ch.outMin = 20000.0f;  // danger 0: open
    ch.outMax = 400.0f;    // danger 1: muffled
    ch.rampMs = 0;
    f.song->addMapping(ch);

    Mapping master = ch;
    master.target = Mapping::Target::MasterCutoff;
    master.outMin = 10000.0f;
    master.outMax = 1000.0f;
    f.song->addMapping(master);
    f.start();

    f.renderSamples(256);
    CHECK(f.engine->channel(0).cutoffHz() == doctest::Approx(20000.0f).epsilon(0.01));

    f.engine->post(Command::setParam(Fixture::kDanger, 1.0f));
    f.renderSamples(512);
    CHECK(f.engine->channel(0).cutoffHz() == doctest::Approx(400.0f).epsilon(0.01));
    CHECK(f.engine->master().getPostFilter().getCutoff() == doctest::Approx(1000.0f).epsilon(0.01));
}

TEST_CASE("Mappings: the filter darkens the channel's sound") {
    auto rms = [](float cutoffHz) {
        Fixture f;
        f.drone->cells[0][0] = Cell::on(45);  // A2, a bright sawtooth
        f.start();
        f.engine->channel(0).setCutoff(cutoffHz, 0);
        f.engine->channel(1).setGain(0.0f);
        f.engine->master().getPostFilter().setCutoff(20000.0f);
        f.renderSamples(4800);
        // Energy of the sample-to-sample difference: a rough "brightness" measure.
        double e = 0;
        for (size_t i = 1; i < f.buf.size(); ++i) e += double(f.buf[i] - f.buf[i - 1]) * (f.buf[i] - f.buf[i - 1]);
        return e;
    };
    CHECK(rms(300.0f) < 0.2 * rms(20000.0f));
}

TEST_CASE("Mappings: tempo scale multiplies the base tempo") {
    Fixture f;
    Mapping m;
    m.param = Fixture::kIntensity;
    m.target = Mapping::Target::TempoScale;
    m.outMin = 1.0f;
    m.outMax = 1.5f;
    m.rampMs = 0;
    f.song->addMapping(m);
    f.start();
    f.renderSamples(256);
    CHECK(f.engine->clock().samplesUntilNextTick() <= 250);

    f.engine->post(Command::setParam(Fixture::kIntensity, 1.0f));
    f.renderSamples(300);
    // 180 BPM: 48000 * 60 / (180 * 96) = 166.67 samples per tick.
    CHECK(f.engine->clock().samplesUntilNextTick() <= 167);
    f.engine->post(Command::setTempo(100.0f, Quantize::Now));
    f.renderSamples(300);
    // Base 100 BPM x 1.5 = 150 BPM: 200 samples per tick.
    TempoClock expected(kSampleRate, 150000);
    CHECK(f.engine->clock().samplesUntilNextTick() <= expected.samplesUntilNextTick());
}

TEST_CASE("Mappings: thresholds fire events when crossed, in both directions") {
    Fixture f;
    Mapping m;
    m.param = Fixture::kDanger;
    m.target = Mapping::Target::Threshold;
    m.index = 0;       // event 0: go to section 1
    m.indexBelow = 1;  // event 1: back to section 0
    m.threshold = 0.6f;
    f.song->addMapping(m);
    f.start();
    f.renderSamples(256);
    CHECK(f.engine->currentSection() == 0);

    f.engine->post(Command::setParam(Fixture::kDanger, 0.5f));
    f.renderSamples(512);
    CHECK(f.engine->currentSection() == 0);
    f.engine->post(Command::setParam(Fixture::kDanger, 0.7f));
    f.renderSamples(512);
    CHECK(f.engine->currentSection() == 1);
    f.engine->post(Command::setParam(Fixture::kDanger, 0.9f));  // still above: no event
    f.renderSamples(512);
    CHECK(f.engine->currentSection() == 1);
    f.engine->post(Command::setParam(Fixture::kDanger, 0.2f));
    f.renderSamples(512);
    CHECK(f.engine->currentSection() == 0);
}

TEST_CASE("Commands: key changes wait for the bar") {
    Fixture f;
    f.start();
    f.renderSamples(1000);
    f.engine->post(Command::setKey(9, scales::kNaturalMinor, Quantize::Bar));
    f.renderSamples(1000);
    CHECK(f.engine->pitch().current.root == 0);
    f.renderSamples(4 * 96 * 250);
    CHECK(f.engine->pitch().current.root == 9);
    CHECK(f.engine->pitch().current.scale == scales::kNaturalMinor);
}

TEST_CASE("Mappings: transpose") {
    Fixture f;
    Mapping m;
    m.param = Fixture::kIntensity;
    m.target = Mapping::Target::Transpose;
    m.outMin = 0.0f;
    m.outMax = 12.0f;
    f.song->addMapping(m);
    f.start();
    f.engine->post(Command::setParam(Fixture::kIntensity, 0.5f));
    f.renderSamples(512);
    CHECK(f.engine->pitch().transpose == 6);
}
