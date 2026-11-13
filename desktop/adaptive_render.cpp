// Plays the adaptive demo through a scripted timeline of parameter changes
// and events, and writes the result to a WAV file.
//
//   adaptive_render [out.wav]

#include <adaptive/engine.hpp>
#include <demo/adaptive_demo.hpp>
#include "wav_writer.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace adaptive;
using Demo = demo::AdaptiveDemo;

namespace {
constexpr uint32_t kSampleRate = 48000;

struct Cue {
    double seconds;
    Command command;
    const char* description;
};

const Cue kTimeline[] = {
    {0.0, Command::setParam(Demo::kIntensity, 0.2f), "intensity 0.2"},
    {0.0, Command::gotoSection(Demo::kExplore, Quantize::Now), "explore"},
    {10.0, Command::setParam(Demo::kIntensity, 0.8f), "intensity 0.8: rhythm layer up, tempo pushes"},
    {15.0, Command::fireEvent(Demo::kPickup), "pickup (on the beat)"},
    {20.0, Command::setParam(Demo::kDanger, 0.9f), "danger 0.9: bridge, then combat at the bar"},
    {31.0, Command::fireEvent(Demo::kHit), "hit"},
    {36.0, Command::setLayerMute(Demo::kMelodyLayer, true), "mute melody layer (at the bar)"},
    {42.0, Command::setLayerMute(Demo::kMelodyLayer, false), "unmute melody layer"},
    {46.0, Command::setParam(Demo::kDanger, 0.1f), "danger 0.1: back to explore, keeping phase"},
    {56.0, Command::setKey(9, scales::kNaturalMinor), "key: A minor (at the bar)"},
    {66.0, Command::stop(), "stop"},
};
constexpr double kEndSeconds = 68.0;
} // namespace

int main(int argc, char** argv) {
    const char* outPath = argc > 1 ? argv[1] : "adaptive_demo.wav";

    auto song = std::make_unique<Demo>();
    auto engine = std::make_unique<Engine>(kSampleRate);
    song->setUp(*engine);

    std::vector<float> out(static_cast<size_t>(kEndSeconds * kSampleRate));
    size_t nextCue = 0;
    constexpr uint32_t kBlock = 256;
    for (size_t pos = 0; pos < out.size(); pos += kBlock) {
        while (nextCue < std::size(kTimeline) && kTimeline[nextCue].seconds * kSampleRate <= pos) {
            engine->post(kTimeline[nextCue].command);
            std::printf("%5.1f s  %s\n", kTimeline[nextCue].seconds, kTimeline[nextCue].description);
            ++nextCue;
        }
        engine->render(out.data() + pos, static_cast<uint32_t>(std::min<size_t>(kBlock, out.size() - pos)));
    }

    if (!desktop::writeWavMono16(outPath, out, kSampleRate)) {
        std::fprintf(stderr, "Failed to write %s\n", outPath);
        return 1;
    }
    std::printf("Wrote %s (%.0f s)\n", outPath, kEndSeconds);
    return 0;
}
