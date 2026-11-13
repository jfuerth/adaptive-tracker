// Renders a short hard-coded demo song to a WAV file, for listening on the desktop.
//
//   render_demo [out.wav] [seconds]
//
// Channel 0: bass, a sequence of two 16-row patterns at 4 rows/beat; the second
//            sweeps the filter (Fxx) and slides into the loop (3xx).
// Channel 1: arpeggio, 12 rows at 3 rows/beat (triplet eighths, one bar).
// Channel 2: chords in 3 columns, 6 rows at 1 row/beat, so it loops every 6 beats
//            and drifts against the 4-beat loops of the other channels.

#include <adaptive/engine.hpp>
#include "wav_writer.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using adaptive::Cell;
using adaptive::Pattern;
namespace fx = adaptive::fx;

namespace {

constexpr uint32_t kSampleRate = 48000;

void bassPatch(synth::WavetableSynth& v) {
    v.setWaveformShape(0.0f);
    v.setBaseCutoff(300.0f);
    v.setFilterQ(4.0f);
    v.setFilterEnvelopeAmount(0.6f);
    v.getAmpEnvelope().setAttackTime(0.002f);
    v.getAmpEnvelope().setDecayTime(0.15f);
    v.getAmpEnvelope().setSustainLevel(0.6f);
    v.getAmpEnvelope().setReleaseTime(0.05f);
    v.setLevel(0.5f);
}

void arpPatch(synth::WavetableSynth& v) {
    v.setWaveformShape(0.8f);
    v.setBaseCutoff(2500.0f);
    v.getAmpEnvelope().setAttackTime(0.001f);
    v.getAmpEnvelope().setDecayTime(0.12f);
    v.getAmpEnvelope().setSustainLevel(0.0f);
    v.getAmpEnvelope().setReleaseTime(0.05f);
    v.setLevel(0.25f);
}

void padPatch(synth::WavetableSynth& v) {
    v.setWaveformShape(0.3f);
    v.setBaseCutoff(900.0f);
    v.getAmpEnvelope().setAttackTime(0.3f);
    v.getAmpEnvelope().setDecayTime(0.5f);
    v.getAmpEnvelope().setSustainLevel(0.7f);
    v.getAmpEnvelope().setReleaseTime(0.6f);
    v.setVibratoDepth(0.08f);
    v.setLevel(0.12f);
}

} // namespace

int main(int argc, char** argv) {
    const char* outPath = argc > 1 ? argv[1] : "demo.wav";
    const double seconds = argc > 2 ? std::atof(argv[2]) : 16.0;

    auto engine = std::make_unique<adaptive::Engine>(kSampleRate);
    engine->setTempoMilliBpm(110000);

    // Patterns live as long as the engine plays them.
    auto bass = std::make_unique<Pattern>();
    bass->numRows = 16;
    bass->rowsPerBeat = 4;
    const uint8_t bassLine[16] = {33, 0, 45, 33, 0, 33, 45, 0, 31, 0, 43, 31, 0, 36, 38, 40};
    for (int r = 0; r < 16; ++r) {
        if (bassLine[r] != 0) bass->cells[r][0] = Cell::on(bassLine[r], r % 4 == 0 ? 127 : 90);
    }

    // Second bar of bass: same line, opening the filter row by row (Fxx), ending
    // with a slide up to the next bar's root (3xx).
    auto bass2 = std::make_unique<Pattern>(*bass);
    for (int r = 0; r < 14; ++r) {
        const Cell& c = bass2->cells[r][0];
        bass2->cells[r][0] = c.with(fx::kFilterCutoff, static_cast<uint8_t>(0x50 + r * 6));
    }
    bass2->cells[14][0] = Cell::on(33).with(fx::kTonePorta, 0x04);
    bass2->cells[15][0] = Cell::effectOnly(fx::kFilterCutoff, 0x50);

    adaptive::Sequence bassSeq;
    bassSeq.append(bass.get());
    bassSeq.append(bass2.get());

    auto arp = std::make_unique<Pattern>();
    arp->numRows = 12;
    arp->rowsPerBeat = 3;
    const uint8_t arpNotes[12] = {69, 72, 76, 69, 72, 76, 67, 71, 74, 67, 71, 74};
    for (int r = 0; r < 12; ++r) arp->cells[r][0] = Cell::on(arpNotes[r], r % 3 == 0 ? 110 : 70);

    auto pad = std::make_unique<Pattern>();
    pad->numRows = 6;
    pad->rowsPerBeat = 1;
    const uint8_t chordA[3] = {57, 60, 64};  // A minor
    const uint8_t chordG[3] = {55, 59, 62};  // G major
    for (int c = 0; c < 3; ++c) {
        pad->cells[0][c] = Cell::on(chordA[c]);
        pad->cells[3][c] = Cell::on(chordG[c]);
    }

    engine->channel(0).forEachVoice(bassPatch);
    engine->channel(0).playSequence(&bassSeq, 0);

    engine->channel(1).forEachVoice(arpPatch);
    engine->channel(1).play(arp.get(), 0);

    engine->channel(2).setNumColumns(3);
    engine->channel(2).forEachVoice(padPatch);
    engine->channel(2).play(pad.get(), 0);

    std::vector<float> out(static_cast<size_t>(seconds * kSampleRate));
    constexpr uint32_t kBlock = 256;
    for (size_t pos = 0; pos < out.size(); pos += kBlock) {
        const uint32_t n = static_cast<uint32_t>(std::min<size_t>(kBlock, out.size() - pos));
        engine->render(out.data() + pos, n);
    }

    if (!desktop::writeWavMono16(outPath, out, kSampleRate)) {
        std::fprintf(stderr, "Failed to write %s\n", outPath);
        return 1;
    }
    std::printf("Wrote %s (%.1f s)\n", outPath, seconds);
    return 0;
}
