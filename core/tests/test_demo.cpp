#include <doctest.h>

#include <demo/adaptive_demo.hpp>

#include <cmath>
#include <memory>
#include <vector>

using namespace adaptive;
using Demo = demo::AdaptiveDemo;

TEST_CASE("Adaptive demo: plays through its sections with bounded output") {
    auto song = std::make_unique<Demo>();
    auto engine = std::make_unique<Engine>(48000);
    song->setUp(*engine);
    engine->post(Command::gotoSection(Demo::kExplore, Quantize::Now));

    std::vector<float> buf(256);
    float peak = 0.0f;
    bool sawBridge = false;
    auto run = [&](double seconds) {
        for (size_t n = 0; n < seconds * 48000 / 256; ++n) {
            engine->render(buf.data(), 256);
            for (float s : buf) {
                REQUIRE(std::isfinite(s));
                peak = std::max(peak, std::fabs(s));
            }
            sawBridge = sawBridge || engine->currentSection() == Demo::kBridge;
        }
    };

    run(3.0);
    CHECK(engine->currentSection() == Demo::kExplore);
    engine->post(Command::setParam(Demo::kDanger, 1.0f));  // crosses the threshold
    run(6.0);
    CHECK(sawBridge);
    CHECK(engine->currentSection() == Demo::kCombat);
    engine->post(Command::fireEvent(Demo::kHit));
    run(0.1);
    CHECK(engine->channel(Demo::kStinger).isOneShot());
    engine->post(Command::setParam(Demo::kDanger, 0.0f));
    run(3.0);
    CHECK(engine->currentSection() == Demo::kExplore);
    CHECK(peak > 0.1f);
    CHECK(peak <= 1.0f);
    CHECK(engine->droppedCommands() == 0);
}
