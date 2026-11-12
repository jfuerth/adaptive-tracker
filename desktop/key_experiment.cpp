// Scale-degree listening experiment (see DESIGN.md, key change).
//
//   key_experiment [out_dir]
//
// Renders one arrangement, written in scale degrees for C major, in several
// keys and scales. For the 5-note pentatonic scales it renders both degree
// mappings (Wrap and Snap) so they can be compared. Writes one WAV per variant
// and all_variants.wav with every variant in order, separated by a short gap.

#include <adaptive/engine.hpp>
#include "wav_writer.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace adaptive;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kTempoMilliBpm = 110000;
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

struct Song {
    Pattern melody, bass, pad, arp;
};

/// Two bars, written as C-major scale degrees. Every degree appears in the melody.
std::unique_ptr<Song> makeSong() {
    auto s = std::make_unique<Song>();

    // Melody: eighth notes. (octave, degree) pairs; degree -1 = note off.
    s->melody.rowsPerBeat = 2;
    s->melody.numRows = 16;
    const int melody[16][2] = {{4, 0}, {4, 2}, {4, 4}, {4, 6}, {5, 0}, {4, 6}, {4, 5}, {4, 4},
                               {4, 3}, {4, 5}, {4, 4}, {4, 2}, {4, 3}, {4, 1}, {4, 0}, {4, -1}};
    for (int r = 0; r < 16; ++r) {
        s->melody.cells[r][0] = melody[r][1] < 0
                                    ? Cell::off()
                                    : Cell::degree(uint8_t(melody[r][0]), uint8_t(melody[r][1]), 0,
                                                   r % 2 == 0 ? 110 : 85);
    }

    // Chords I, IV, V, vi, two beats each.
    const int roots[4] = {0, 3, 4, 5};

    s->bass.rowsPerBeat = 1;
    s->bass.numRows = 8;
    for (int i = 0; i < 4; ++i) {
        s->bass.cells[i * 2][0] = Cell::degree(2, uint8_t(roots[i]), 0, 120);
        s->bass.cells[i * 2 + 1][0] = Cell::degree(2, uint8_t(roots[i]), 0, 80);
    }

    s->pad.rowsPerBeat = 1;
    s->pad.numRows = 8;
    for (int i = 0; i < 4; ++i) {
        for (int c = 0; c < 3; ++c) {
            s->pad.cells[i * 2][c] = Cell::degree(3, uint8_t(roots[i] + 2 * c));
        }
    }

    // Degree-mode arpeggio 0x24: root, 3rd and 5th of whatever scale is current.
    s->arp.rowsPerBeat = 4;
    s->arp.numRows = 32;
    for (int i = 0; i < 4; ++i) {
        for (int k = 0; k < 8; k += 2) {
            s->arp.cells[i * 8 + k][0] = Cell::degree(5, uint8_t(roots[i]), 0, 60).with(fx::kArpeggio, 0x24);
        }
    }
    return s;
}

void leadPatch(synth::WavetableSynth& v) {
    v.setWaveformShape(0.5f);
    v.setBaseCutoff(2000.0f);
    v.getAmpEnvelope().setAttackTime(0.005f);
    v.getAmpEnvelope().setDecayTime(0.2f);
    v.getAmpEnvelope().setSustainLevel(0.5f);
    v.getAmpEnvelope().setReleaseTime(0.08f);
    v.setLevel(0.3f);
}
void bassPatch(synth::WavetableSynth& v) {
    v.setWaveformShape(0.0f);
    v.setBaseCutoff(350.0f);
    v.setFilterQ(3.0f);
    v.getAmpEnvelope().setAttackTime(0.002f);
    v.getAmpEnvelope().setDecayTime(0.2f);
    v.getAmpEnvelope().setSustainLevel(0.5f);
    v.getAmpEnvelope().setReleaseTime(0.05f);
    v.setLevel(0.45f);
}
void padPatch(synth::WavetableSynth& v) {
    v.setWaveformShape(0.3f);
    v.setBaseCutoff(900.0f);
    v.getAmpEnvelope().setAttackTime(0.15f);
    v.getAmpEnvelope().setDecayTime(0.4f);
    v.getAmpEnvelope().setSustainLevel(0.7f);
    v.getAmpEnvelope().setReleaseTime(0.4f);
    v.setLevel(0.1f);
}
void arpPatch(synth::WavetableSynth& v) {
    v.setWaveformShape(1.0f);
    v.setBaseCutoff(3000.0f);
    v.getAmpEnvelope().setAttackTime(0.001f);
    v.getAmpEnvelope().setDecayTime(0.1f);
    v.getAmpEnvelope().setSustainLevel(0.2f);
    v.getAmpEnvelope().setReleaseTime(0.05f);
    v.setLevel(0.08f);
}

std::vector<float> renderVariant(const Song& song, const Variant& variant) {
    auto engine = std::make_unique<Engine>(kSampleRate);
    engine->clock().setTempoMilliBpm(kTempoMilliBpm);
    engine->pitch().home = Key{0, scales::kMajor};
    engine->pitch().current = variant.key;
    engine->pitch().mapping = variant.mapping;

    struct Part { const Pattern* pattern; void (*patch)(synth::WavetableSynth&); uint32_t columns; };
    const Part parts[] = {{&song.melody, leadPatch, 1}, {&song.bass, bassPatch, 1},
                          {&song.pad, padPatch, 3}, {&song.arp, arpPatch, 1}};
    uint32_t i = 0;
    for (const Part& part : parts) {
        Channel& ch = engine->channel(i++);
        ch.setPitchMode(PitchMode::Degree);
        ch.setNumColumns(part.columns);
        ch.forEachVoice(part.patch);
        ch.play(part.pattern, 0);
    }

    const double seconds = kBars * 4 * 60.0 / (kTempoMilliBpm / 1000.0) + 0.5;  // + release tail
    std::vector<float> out(static_cast<size_t>(seconds * kSampleRate));
    const size_t stopAt = out.size() - kSampleRate / 2;
    for (size_t pos = 0; pos < out.size(); pos += 256) {
        if (pos >= stopAt && pos < stopAt + 256) {
            for (uint32_t c = 0; c < 4; ++c) engine->channel(c).play(nullptr, 0);
        }
        engine->render(out.data() + pos, static_cast<uint32_t>(std::min<size_t>(256, out.size() - pos)));
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    const auto song = makeSong();

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
