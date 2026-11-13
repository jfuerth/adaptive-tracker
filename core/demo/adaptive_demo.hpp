#pragma once

#include <adaptive/engine.hpp>

#include <cstdint>

namespace adaptive::demo {

/**
 * Adaptive demo song: two sections, a bridge, three layers, two game
 * parameters with mappings, and game events with stingers. Written in C-major
 * scale degrees (drums excepted), so key and scale changes affect everything.
 *
 * Holds its own patterns and Song, so it must outlive any engine playing it.
 * Large: allocate it on the heap.
 */
class AdaptiveDemo {
public:
    // Channels.
    static constexpr uint8_t kLead = 0, kBass = 1, kPad = 2, kArp = 3, kDrums = 4, kStinger = 5;
    // Sections.
    static constexpr uint8_t kExplore = 0, kCombat = 1, kBridge = 2;
    // Layers.
    static constexpr uint8_t kRhythmLayer = 0, kMelodyLayer = 1, kPadLayer = 2;
    // Game parameters.
    static constexpr uint8_t kIntensity = 0, kDanger = 1;
    // Game events.
    static constexpr uint8_t kHit = 0, kPickup = 1, kEnterCombat = 2, kCalmDown = 3, kAlarm = 4;

    static constexpr const char* kSectionNames[] = {"Explore", "Combat", "Bridge"};
    static constexpr const char* kLayerNames[] = {"Rhythm", "Melody", "Pad"};
    static constexpr const char* kParamNames[] = {"Intensity", "Danger"};

    AdaptiveDemo() { build(); }

    const Song& song() const { return song_; }

    /// Patches, pitch modes, column counts, and the song. Call before audio
    /// starts. Then post Command::gotoSection(kExplore, Quantize::Now) to play.
    void setUp(Engine& engine) const {
        auto setUpChannel = [&](uint8_t ch, PitchMode mode, uint32_t columns, void (*patch)(synth::WavetableSynth&)) {
            Channel& c = engine.channel(ch);
            c.setPitchMode(mode);
            c.setNumColumns(columns);
            c.forEachVoice(patch);
        };
        setUpChannel(kLead, PitchMode::Degree, 1, leadPatch);
        setUpChannel(kBass, PitchMode::Degree, 1, bassPatch);
        setUpChannel(kPad, PitchMode::Degree, 3, padPatch);
        setUpChannel(kArp, PitchMode::Degree, 1, arpPatch);
        setUpChannel(kDrums, PitchMode::Absolute, 2, kickPatch);
        hatPatch(engine.channel(kDrums).voice(1));
        setUpChannel(kStinger, PitchMode::Degree, 1, stingerPatch);

        engine.pitch().home = Key{0, scales::kMajor};
        engine.setSong(&song_);
    }

private:
    /// Set a degree note, or an "off" when degree < 0.
    static void put(Pattern& p, int row, int col, int octave, int degree, uint8_t vol = Cell::kVolumeDefault) {
        p.cells[row][col] = degree < 0 ? Cell::off() : Cell::degree(uint8_t(octave), uint8_t(degree), 0, vol);
    }

    static void chordPattern(Pattern& p, const int (&roots)[4], int rowsPerBar) {
        p.rowsPerBeat = 1;
        p.numRows = uint16_t(rowsPerBar * 4);
        for (int bar = 0; bar < 4; ++bar) {
            for (int c = 0; c < 3; ++c) put(p, bar * rowsPerBar, c, 3, roots[bar] + 2 * c);
        }
    }

    void build() {
        const int exploreRoots[4] = {0, 5, 3, 4};  // I vi IV V
        const int combatRoots[4] = {5, 3, 4, 4};   // vi IV V V

        // ---- Explore (96 BPM, 4 bars) ----
        chordPattern(explorePad_, exploreRoots, 4);

        exploreBass_.rowsPerBeat = 1;
        exploreBass_.numRows = 16;
        for (int bar = 0; bar < 4; ++bar) {
            put(exploreBass_, bar * 4, 0, 2, exploreRoots[bar], 120);
            put(exploreBass_, bar * 4 + 2, 0, 2, exploreRoots[bar] + 4, 90);
        }

        exploreLead_.rowsPerBeat = 2;
        exploreLead_.numRows = 32;
        const int leadA[32][2] = {
            {4, 4}, {0, -2}, {4, 2}, {0, -2}, {4, 4}, {4, 5}, {4, 4}, {0, -2},
            {4, 2}, {0, -2}, {4, 0}, {0, -2}, {3, 5}, {0, -2}, {0, -1}, {0, -2},
            {4, 3}, {0, -2}, {4, 5}, {0, -2}, {5, 0}, {0, -2}, {4, 5}, {4, 3},
            {4, 4}, {0, -2}, {0, -2}, {0, -2}, {4, 1}, {0, -2}, {3, 6}, {0, -1},
        };  // {octave, degree}; degree -1 = off, -2 = empty
        for (int r = 0; r < 32; ++r) {
            if (leadA[r][1] != -2) put(exploreLead_, r, 0, leadA[r][0], leadA[r][1], 100);
        }

        exploreArp_.rowsPerBeat = 4;
        exploreArp_.numRows = 64;
        for (int bar = 0; bar < 4; ++bar) {
            for (int k = 0; k < 16; k += 2) {
                exploreArp_.cells[bar * 16 + k][0] =
                    Cell::degree(5, uint8_t(exploreRoots[bar]), 0, 50).with(fx::kArpeggio, 0x24);
            }
        }

        // ---- Combat (128 BPM, 4 bars) ----
        chordPattern(combatPad_, combatRoots, 4);

        combatBass_.rowsPerBeat = 4;
        combatBass_.numRows = 64;
        for (int bar = 0; bar < 4; ++bar) {
            for (int k = 0; k < 16; k += 2) {
                put(combatBass_, bar * 16 + k, 0, (k % 4 == 0) ? 2 : 3, combatRoots[bar], k % 4 == 0 ? 127 : 80);
            }
        }

        combatLead_.rowsPerBeat = 4;
        combatLead_.numRows = 64;
        const int motif[6][2] = {{0, 4}, {3, 2}, {6, 4}, {8, 7}, {11, 6}, {14, 4}};  // {row, degree above root}
        for (int bar = 0; bar < 4; ++bar) {
            for (const auto& m : motif) {
                put(combatLead_, bar * 16 + m[0], 0, 4, combatRoots[bar] + m[1], 110);
                if (m[0] + 2 < 16) put(combatLead_, bar * 16 + m[0] + 2, 0, 0, -1);
            }
        }

        combatArp_.rowsPerBeat = 4;
        combatArp_.numRows = 64;
        for (int bar = 0; bar < 4; ++bar) {
            for (int k = 0; k < 16; ++k) {
                combatArp_.cells[bar * 16 + k][0] =
                    Cell::degree(5, uint8_t(combatRoots[bar]), 0, k % 4 == 0 ? 70 : 45).with(fx::kArpeggio, 0x24);
            }
        }

        combatDrums_.rowsPerBeat = 4;
        combatDrums_.numRows = 16;
        for (int beat = 0; beat < 4; ++beat) {
            combatDrums_.cells[beat * 4][0] = Cell::on(45, 127).with(fx::kPortaDown, 0x60);
            combatDrums_.cells[beat * 4 + 2][1] = Cell::on(108, 60);
        }

        // ---- Bridge (1 bar riser into combat) ----
        bridgeBass_.rowsPerBeat = 1;
        bridgeBass_.numRows = 4;
        put(bridgeBass_, 0, 0, 2, 4, 127);  // the V chord's root, held

        bridgeArp_.rowsPerBeat = 4;
        bridgeArp_.numRows = 16;
        for (int k = 0; k < 16; ++k) {
            bridgeArp_.cells[k][0] = Cell::degree(uint8_t(4 + k / 8), uint8_t(k % 8), 0, uint8_t(50 + k * 4));
        }

        bridgeDrums_.rowsPerBeat = 4;
        bridgeDrums_.numRows = 16;
        for (int k = 0; k < 16; ++k) {
            if (k < 8 ? (k % 4 == 0) : (k >= 12 || k % 2 == 0)) {
                bridgeDrums_.cells[k][0] = Cell::on(45, uint8_t(80 + k * 3)).with(fx::kPortaDown, 0x60);
            }
        }

        // ---- Stingers ----
        hit_.rowsPerBeat = 8;
        hit_.numRows = 6;
        hit_.cells[0][0] = Cell::degree(5, 4, 0, 127).with(fx::kPortaDown, 0x18);
        hit_.cells[5][0] = Cell::off();

        pickup_.rowsPerBeat = 8;
        pickup_.numRows = 6;
        put(pickup_, 0, 0, 5, 0, 110);
        put(pickup_, 1, 0, 5, 2, 110);
        put(pickup_, 2, 0, 5, 4, 110);
        put(pickup_, 3, 0, 6, 0, 120);
        put(pickup_, 5, 0, 0, -1);

        // ---- Sequences and sections ----
        auto one = [](Sequence& s, const Pattern& p) { s.append(&p); };
        one(seqExploreLead_, exploreLead_);
        one(seqExploreBass_, exploreBass_);
        one(seqExplorePad_, explorePad_);
        one(seqExploreArp_, exploreArp_);
        one(seqCombatLead_, combatLead_);
        one(seqCombatBass_, combatBass_);
        one(seqCombatPad_, combatPad_);
        one(seqCombatArp_, combatArp_);
        one(seqCombatDrums_, combatDrums_);
        one(seqBridgeBass_, bridgeBass_);
        one(seqBridgeArp_, bridgeArp_);
        one(seqBridgeDrums_, bridgeDrums_);

        Section& explore = song_.sections[kExplore];
        explore.sequences[kLead] = &seqExploreLead_;
        explore.sequences[kBass] = &seqExploreBass_;
        explore.sequences[kPad] = &seqExplorePad_;
        explore.sequences[kArp] = &seqExploreArp_;
        explore.tempoMilliBpm = 96000;

        Section& combat = song_.sections[kCombat];
        combat.sequences[kLead] = &seqCombatLead_;
        combat.sequences[kBass] = &seqCombatBass_;
        combat.sequences[kPad] = &seqCombatPad_;
        combat.sequences[kArp] = &seqCombatArp_;
        combat.sequences[kDrums] = &seqCombatDrums_;
        combat.tempoMilliBpm = 128000;

        Section& bridge = song_.sections[kBridge];
        bridge.sequences[kBass] = &seqBridgeBass_;
        bridge.sequences[kArp] = &seqBridgeArp_;
        bridge.sequences[kDrums] = &seqBridgeDrums_;
        bridge.tempoMilliBpm = 128000;
        bridge.bridgeBeats = 4;
        song_.numSections = 3;

        // ---- Layers ----
        song_.layers[kRhythmLayer] = (1u << kArp) | (1u << kDrums);
        song_.layers[kMelodyLayer] = 1u << kLead;
        song_.layers[kPadLayer] = 1u << kPad;
        song_.numLayers = 3;

        // ---- Events ----
        song_.events[kHit].add(Command::playStinger(kStinger, &hit_, Quantize::Now));
        song_.events[kPickup].add(Command::playStinger(kStinger, &pickup_, Quantize::Beat));
        song_.events[kEnterCombat].add(Command::gotoSection(kCombat, Quantize::Bar, Entry::Restart, kBridge));
        song_.events[kCalmDown].add(Command::gotoSection(kExplore, Quantize::Bar, Entry::KeepPhase));
        song_.events[kAlarm].add(Command::gotoSection(kCombat, Quantize::Now, Entry::Restart));
        song_.events[kAlarm].add(Command::playStinger(kStinger, &hit_, Quantize::Now));
        song_.numEvents = 5;

        // ---- Mappings ----
        Mapping m;
        m.param = kIntensity;
        m.target = Mapping::Target::LayerGain;
        m.index = kRhythmLayer;
        m.outMin = 0.15f;
        m.outMax = 1.0f;
        m.rampMs = 400;
        song_.addMapping(m);

        m.target = Mapping::Target::TempoScale;
        m.outMin = 0.94f;
        m.outMax = 1.10f;
        m.rampMs = 1500;
        song_.addMapping(m);

        m.target = Mapping::Target::ChannelCutoff;
        m.index = kArp;
        m.curve = Mapping::Curve::Exponential;
        m.outMin = 1200.0f;
        m.outMax = 14000.0f;
        m.rampMs = 200;
        song_.addMapping(m);

        m.param = kDanger;
        m.index = kPad;
        m.outMin = 6000.0f;
        m.outMax = 700.0f;
        m.rampMs = 300;
        song_.addMapping(m);

        Mapping t;
        t.param = kDanger;
        t.target = Mapping::Target::Threshold;
        t.threshold = 0.75f;
        t.index = kEnterCombat;
        t.indexBelow = kCalmDown;
        song_.addMapping(t);
    }

    static void leadPatch(synth::WavetableSynth& v) {
        v.setWaveformShape(0.5f);
        v.setBaseCutoff(2200.0f);
        v.getAmpEnvelope().setAttackTime(0.005f);
        v.getAmpEnvelope().setDecayTime(0.25f);
        v.getAmpEnvelope().setSustainLevel(0.5f);
        v.getAmpEnvelope().setReleaseTime(0.12f);
        v.setVibratoDepth(0.05f);
        v.setLevel(0.28f);
    }
    static void bassPatch(synth::WavetableSynth& v) {
        v.setWaveformShape(0.0f);
        v.setBaseCutoff(320.0f);
        v.setFilterQ(3.0f);
        v.setFilterEnvelopeAmount(0.5f);
        v.getAmpEnvelope().setAttackTime(0.002f);
        v.getAmpEnvelope().setDecayTime(0.2f);
        v.getAmpEnvelope().setSustainLevel(0.6f);
        v.getAmpEnvelope().setReleaseTime(0.06f);
        v.setLevel(0.42f);
    }
    static void padPatch(synth::WavetableSynth& v) {
        v.setWaveformShape(0.3f);
        v.setBaseCutoff(1000.0f);
        v.getAmpEnvelope().setAttackTime(0.25f);
        v.getAmpEnvelope().setDecayTime(0.5f);
        v.getAmpEnvelope().setSustainLevel(0.7f);
        v.getAmpEnvelope().setReleaseTime(0.6f);
        v.setVibratoDepth(0.06f);
        v.setLevel(0.1f);
    }
    static void arpPatch(synth::WavetableSynth& v) {
        v.setWaveformShape(1.0f);
        v.setBaseCutoff(4000.0f);
        v.getAmpEnvelope().setAttackTime(0.001f);
        v.getAmpEnvelope().setDecayTime(0.09f);
        v.getAmpEnvelope().setSustainLevel(0.15f);
        v.getAmpEnvelope().setReleaseTime(0.05f);
        v.setLevel(0.1f);
    }
    static void kickPatch(synth::WavetableSynth& v) {
        v.setWaveformShape(0.5f);
        v.setBaseCutoff(900.0f);
        v.getAmpEnvelope().setAttackTime(0.001f);
        v.getAmpEnvelope().setDecayTime(0.18f);
        v.getAmpEnvelope().setSustainLevel(0.0f);
        v.getAmpEnvelope().setReleaseTime(0.05f);
        v.setLevel(0.85f);
    }
    static void hatPatch(synth::WavetableSynth& v) {
        v.setWaveformShape(1.0f);
        v.setBaseCutoff(12000.0f);
        v.getAmpEnvelope().setAttackTime(0.0005f);
        v.getAmpEnvelope().setDecayTime(0.03f);
        v.getAmpEnvelope().setSustainLevel(0.0f);
        v.getAmpEnvelope().setReleaseTime(0.02f);
        v.setLevel(0.12f);
    }
    static void stingerPatch(synth::WavetableSynth& v) {
        v.setWaveformShape(0.9f);
        v.setBaseCutoff(5000.0f);
        v.setFilterQ(2.0f);
        v.getAmpEnvelope().setAttackTime(0.001f);
        v.getAmpEnvelope().setDecayTime(0.15f);
        v.getAmpEnvelope().setSustainLevel(0.4f);
        v.getAmpEnvelope().setReleaseTime(0.15f);
        v.setLevel(0.3f);
    }

    Song song_;
    Pattern exploreLead_, exploreBass_, explorePad_, exploreArp_;
    Pattern combatLead_, combatBass_, combatPad_, combatArp_, combatDrums_;
    Pattern bridgeBass_, bridgeArp_, bridgeDrums_;
    Pattern hit_, pickup_;
    Sequence seqExploreLead_, seqExploreBass_, seqExplorePad_, seqExploreArp_;
    Sequence seqCombatLead_, seqCombatBass_, seqCombatPad_, seqCombatArp_, seqCombatDrums_;
    Sequence seqBridgeBass_, seqBridgeArp_, seqBridgeDrums_;
};

} // namespace adaptive::demo
