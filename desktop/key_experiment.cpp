// Scale-degree listening experiment (see DESIGN.md, key change).
//
//   key_experiment [out_dir]
//
// Renders one arrangement, written in scale degrees for C major, in several
// keys and scales. For the 5-note pentatonic scales it renders both degree
// mappings (Wrap and Snap) so they can be compared. Writes one WAV per variant
// and all_variants.wav with every variant in order, separated by a short gap.

#include <adaptive/engine.hpp>
#include <demo/degree_demo.hpp>
#include "wav_writer.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace adaptive;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kTempoMilliBpm = demo::DegreeDemo::kDefaultTempoMilliBpm;
constexpr int kBars = 4;  // the 2-bar arrangement plays twice

struct Variant {
    const char* name;
    Key key;
    DegreeMapping mapping;
};

const Variant kVariants[] = {
    {"01_c_major", {0, scales::kMajor}, DegreeMapping::Wrap},
    {"02_c_minor", {0, scales::kNaturalMinor}, DegreeMapping::Wrap},
    {"03_c_harmonic_minor", {0, scales::kHarmonicMinor}, DegreeMapping::Wrap},
    {"04_d_dorian", {2, scales::kDorian}, DegreeMapping::Wrap},
    {"05_c_phrygian", {0, scales::kPhrygian}, DegreeMapping::Wrap},
    {"06_c_major_pentatonic_wrap", {0, scales::kMajorPentatonic}, DegreeMapping::Wrap},
    {"07_c_major_pentatonic_snap", {0, scales::kMajorPentatonic}, DegreeMapping::Snap},
    {"08_c_minor_pentatonic_wrap", {0, scales::kMinorPentatonic}, DegreeMapping::Wrap},
    {"09_c_minor_pentatonic_snap", {0, scales::kMinorPentatonic}, DegreeMapping::Snap},
};

std::vector<float> renderVariant(const demo::DegreeDemo& song, const Variant& variant) {
    auto engine = std::make_unique<Engine>(kSampleRate);
    engine->setTempoMilliBpm(kTempoMilliBpm);
    song.setUpChannels(*engine);
    engine->pitch().current = variant.key;
    engine->pitch().mapping = variant.mapping;
    song.start(*engine, 0);

    const double seconds = kBars * 4 * 60.0 / (kTempoMilliBpm / 1000.0) + 0.5;  // + release tail
    std::vector<float> out(static_cast<size_t>(seconds * kSampleRate));
    const size_t stopAt = out.size() - kSampleRate / 2;
    for (size_t pos = 0; pos < out.size(); pos += 256) {
        if (pos >= stopAt && pos < stopAt + 256) song.stop(*engine);
        engine->render(out.data() + pos, static_cast<uint32_t>(std::min<size_t>(256, out.size() - pos)));
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    const auto song = std::make_unique<demo::DegreeDemo>();

    std::vector<float> all;
    for (const Variant& v : kVariants) {
        const auto audio = renderVariant(*song, v);
        const std::string path = dir + "/" + v.name + ".wav";
        if (!desktop::writeWavMono16(path.c_str(), audio, kSampleRate)) {
            std::fprintf(stderr, "Failed to write %s\n", path.c_str());
            return 1;
        }
        std::printf("Wrote %s\n", path.c_str());
        all.insert(all.end(), audio.begin(), audio.end());
        all.insert(all.end(), kSampleRate, 0.0f);  // 1 s gap
    }
    const std::string allPath = dir + "/all_variants.wav";
    if (!desktop::writeWavMono16(allPath.c_str(), all, kSampleRate)) return 1;
    std::printf("Wrote %s\n", allPath.c_str());
    return 0;
}
