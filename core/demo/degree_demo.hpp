#pragma once

#include <adaptive/engine.hpp>

#include <cstdint>

namespace adaptive::demo {

/**
 * A two-bar arrangement written in C-major scale degrees, used by the
 * key_experiment desktop tool and the Android demo. All four channels are in
 * Degree mode, so the whole arrangement follows key and scale changes.
 *
 * Channel 0: melody, eighth notes; every degree appears.
 * Channel 1: bass on chord roots I, IV, V, vi.
 * Channel 2: pad, three columns playing triads.
 * Channel 3: arpeggio 0x24 (root, 3rd, 5th of the current scale).
 *
 * Holds its own patterns, so it must outlive any engine playing it. Big
 * (four Patterns), so allocate it on the heap.
 */
class DegreeDemo {
public:
    static constexpr uint32_t kNumChannels = 4;
    static constexpr uint32_t kDefaultTempoMilliBpm = 110000;

    DegreeDemo() { build(); }

    /// Patches, pitch modes and column counts. Call once per engine, before start().
    void setUpChannels(Engine& engine) const {
        struct Part { void (*patch)(synth::WavetableSynth&); uint32_t columns; };
        const Part parts[kNumChannels] = {{leadPatch, 1}, {bassPatch, 1}, {padPatch, 3}, {arpPatch, 1}};
        for (uint32_t i = 0; i < kNumChannels; ++i) {
            Channel& ch = engine.channel(i);
            ch.setPitchMode(PitchMode::Degree);
            ch.setNumColumns(parts[i].columns);
            ch.forEachVoice(parts[i].patch);
        }
        engine.pitch().home = Key{0, scales::kMajor};
    }

    /// Start all channels together at `startTick`.
    void start(Engine& engine, uint64_t startTick) const {
        const Pattern* patterns[kNumChannels] = {&melody_, &bass_, &pad_, &arp_};
        for (uint32_t i = 0; i < kNumChannels; ++i) engine.channel(i).play(patterns[i], startTick);
    }

    /// Stop all channels; sounding notes release naturally.
    void stop(Engine& engine) const {
        for (uint32_t i = 0; i < kNumChannels; ++i) engine.channel(i).play(nullptr, 0);
    }

    /// Length of one pass through the arrangement, in beats.
    static constexpr uint32_t lengthBeats() { return 8; }

private:
    void build() {
        // Melody: (octave, degree) pairs; degree -1 = note off.
        melody_.ticksPerRow = 12;
        melody_.numRows = 16;
        const int melody[16][2] = {{4, 0}, {4, 2}, {4, 4}, {4, 6}, {5, 0}, {4, 6}, {4, 5}, {4, 4},
                                   {4, 3}, {4, 5}, {4, 4}, {4, 2}, {4, 3}, {4, 1}, {4, 0}, {4, -1}};
        for (int r = 0; r < 16; ++r) {
            melody_.cells[r][0] = melody[r][1] < 0
                                      ? Cell::off()
                                      : Cell::degree(uint8_t(melody[r][0]), uint8_t(melody[r][1]), 0,
                                                     r % 2 == 0 ? 110 : 85);
        }

        // Chords I, IV, V, vi, two beats each.
        const int roots[4] = {0, 3, 4, 5};

        bass_.ticksPerRow = 24;
        bass_.numRows = 8;
        for (int i = 0; i < 4; ++i) {
            bass_.cells[i * 2][0] = Cell::degree(2, uint8_t(roots[i]), 0, 120);
            bass_.cells[i * 2 + 1][0] = Cell::degree(2, uint8_t(roots[i]), 0, 80);
        }

        pad_.ticksPerRow = 24;
        pad_.numRows = 8;
        for (int i = 0; i < 4; ++i) {
            for (int c = 0; c < 3; ++c) pad_.cells[i * 2][c] = Cell::degree(3, uint8_t(roots[i] + 2 * c));
        }

        arp_.ticksPerRow = 6;
        arp_.numRows = 32;
        for (int i = 0; i < 4; ++i) {
            for (int k = 0; k < 8; k += 2) {
                arp_.cells[i * 8 + k][0] = Cell::degree(5, uint8_t(roots[i]), 0, 60).with(fx::kArpeggio, 0x24);
            }
        }
    }

    static void leadPatch(synth::WavetableSynth& v) {
        v.setWaveformShape(0.5f);
        v.setBaseCutoff(2000.0f);
        v.getAmpEnvelope().setAttackTime(0.005f);
        v.getAmpEnvelope().setDecayTime(0.2f);
        v.getAmpEnvelope().setSustainLevel(0.5f);
        v.getAmpEnvelope().setReleaseTime(0.08f);
        v.setLevel(0.3f);
    }
    static void bassPatch(synth::WavetableSynth& v) {
        v.setWaveformShape(0.0f);
        v.setBaseCutoff(350.0f);
        v.setFilterQ(3.0f);
        v.getAmpEnvelope().setAttackTime(0.002f);
        v.getAmpEnvelope().setDecayTime(0.2f);
        v.getAmpEnvelope().setSustainLevel(0.5f);
        v.getAmpEnvelope().setReleaseTime(0.05f);
        v.setLevel(0.45f);
    }
    static void padPatch(synth::WavetableSynth& v) {
        v.setWaveformShape(0.3f);
        v.setBaseCutoff(900.0f);
        v.getAmpEnvelope().setAttackTime(0.15f);
        v.getAmpEnvelope().setDecayTime(0.4f);
        v.getAmpEnvelope().setSustainLevel(0.7f);
        v.getAmpEnvelope().setReleaseTime(0.4f);
        v.setLevel(0.1f);
    }
    static void arpPatch(synth::WavetableSynth& v) {
        v.setWaveformShape(1.0f);
        v.setBaseCutoff(3000.0f);
        v.getAmpEnvelope().setAttackTime(0.001f);
        v.getAmpEnvelope().setDecayTime(0.1f);
        v.getAmpEnvelope().setSustainLevel(0.2f);
        v.getAmpEnvelope().setReleaseTime(0.05f);
        v.setLevel(0.08f);
    }

    Pattern melody_, bass_, pad_, arp_;
};

} // namespace adaptive::demo
