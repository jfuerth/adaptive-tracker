#pragma once

#include <adaptive/engine.hpp>

#include <cstdint>

namespace adaptive::demo {

/**
 * Adaptive demo song: two sections, a bridge, three layers, two
 * parameters with mappings, and events with stingers. Written in C-major
 * scale degrees (drums excepted), so key and scale changes affect everything.
 *
 * Builds a Song, which must outlive any engine playing it. Large: allocate
 * it on the heap.
 */
class AdaptiveDemo {
public:
    // Channels.
    static constexpr uint8_t kLead = 0, kBass = 1, kPad = 2, kArp = 3, kDrums = 4, kStinger = 5;
    // Sections.
    static constexpr uint8_t kExplore = 0, kCombat = 1, kBridge = 2;
    // Layers.
    static constexpr uint8_t kRhythmLayer = 0, kMelodyLayer = 1, kPadLayer = 2;
    // Parameters.
    static constexpr uint8_t kIntensity = 0, kDanger = 1;
    // Events.
    static constexpr uint8_t kHit = 0, kPickup = 1, kEnterCombat = 2, kCalmDown = 3, kAlarm = 4;

    static constexpr const char* kSectionNames[] = {"Explore", "Combat", "Bridge"};
    static constexpr const char* kLayerNames[] = {"Rhythm", "Melody", "Pad"};
    static constexpr const char* kParamNames[] = {"Intensity", "Danger"};
    static constexpr const char* kChannelNames[] = {"Lead", "Bass", "Pad", "Arp", "Drums", "Stinger"};
    static constexpr uint8_t kNumChannels = 6;
    // Instruments.
    static constexpr uint8_t kLeadSynth = 0, kBassSynth = 1, kPadSynth = 2, kArpSynth = 3, kKick = 4, kHat = 5,
                             kStingerSynth = 6;
    static constexpr const char* kInstrumentNames[] = {"Lead", "Bass", "Pad", "Arp", "Kick", "Hat", "Stinger"};

    // Patterns, in the song's pattern table: grouped by channel, the order a
    // song file lists them in.
    static constexpr uint8_t kExploreLead = 0, kCombatLead = 1, kExploreBass = 2, kCombatBass = 3,
                             kBridgeBass = 4, kExplorePad = 5, kCombatPad = 6, kExploreArp = 7, kCombatArp = 8,
                             kBridgeArp = 9, kCombatDrums = 10, kBridgeDrums = 11, kHitStinger = 12,
                             kPickupStinger = 13;
    static constexpr const char* kPatternNames[] = {
        "Explore lead", "Combat lead", "Explore bass", "Combat bass", "Bridge bass", "Explore pad", "Combat pad",
        "Explore arp", "Combat arp", "Bridge arp", "Combat drums", "Bridge drums", "Hit stinger", "Pickup stinger"};
    static constexpr const char* kEventNames[] = {"Hit", "Pickup", "Enter combat", "Calm down", "Alarm"};
    static constexpr const char* kTitle = "Adaptive demo";

    AdaptiveDemo() { build(); }

    const Song& song() const { return song_; }
    Song& song() { return song_; }

    /// Hand the song to the engine. Call before audio starts, then post
    /// Command::gotoSection(kExplore, Quantize::Now) to play.
    void setUp(Engine& engine) { engine.setSong(&song_); }

private:
    /// Set a degree note, or an "off" when degree < 0.
    static void put(Pattern& p, int row, int col, int octave, int degree, uint8_t vol = Cell::kVolumeDefault) {
        p.cells[row][col] = degree < 0 ? Cell::off() : Cell::degree(uint8_t(octave), uint8_t(degree), 0, vol);
    }

    /// Add a pattern to the song (in kPatternNames order) and return it.
    Pattern& add(uint8_t channel, uint16_t numRows, uint8_t ticksPerRow) {
        return song_.patterns[song_.addPattern(channel, numRows, ticksPerRow)];
    }

    static void chordPattern(Pattern& p, const int (&roots)[4], int rowsPerBar) {
        for (int bar = 0; bar < 4; ++bar) {
            for (int c = 0; c < 3; ++c) put(p, bar * rowsPerBar, c, 3, roots[bar] + 2 * c);
        }
    }

    void build() {
        const int exploreRoots[4] = {0, 5, 3, 4};  // I vi IV V
        const int combatRoots[4] = {5, 3, 4, 4};   // vi IV V V
        song_.tempoMilliBpm = 96000;
        song_.homeKey = Key{0, scales::kMajor};
        song_.startSection = kExplore;
        song_.numParams = 2;
        song_.paramInitial[kIntensity] = 0.2f;  // a little rhythm from the start
        song_.addChannel(PitchMode::Degree, 1);    // lead
        song_.addChannel(PitchMode::Degree, 1);    // bass
        song_.addChannel(PitchMode::Degree, 3);    // pad
        song_.addChannel(PitchMode::Degree, 1);    // arp
        song_.addChannel(PitchMode::Absolute, 2);  // drums
        song_.addChannel(PitchMode::Degree, 1);    // stinger
        buildInstruments();

        // Speeds (ticks per row): 24 = 1 row per beat, 12 = 2, 6 = 4, 3 = 8.
        Pattern& exploreLead_ = add(kLead, 32, 12);
        Pattern& combatLead_ = add(kLead, 64, 6);
        Pattern& exploreBass_ = add(kBass, 16, 24);
        Pattern& combatBass_ = add(kBass, 64, 6);
        Pattern& bridgeBass_ = add(kBass, 4, 24);
        Pattern& explorePad_ = add(kPad, 16, 24);
        Pattern& combatPad_ = add(kPad, 16, 24);
        Pattern& exploreArp_ = add(kArp, 64, 6);
        Pattern& combatArp_ = add(kArp, 64, 6);
        Pattern& bridgeArp_ = add(kArp, 16, 6);
        Pattern& combatDrums_ = add(kDrums, 16, 6);
        Pattern& bridgeDrums_ = add(kDrums, 16, 6);
        Pattern& hit_ = add(kStinger, 6, 3);
        Pattern& pickup_ = add(kStinger, 6, 3);

        // ---- Explore (96 BPM, 4 bars) ----
        chordPattern(explorePad_, exploreRoots, 4);

        for (int bar = 0; bar < 4; ++bar) {
            put(exploreBass_, bar * 4, 0, 2, exploreRoots[bar], 120);
            put(exploreBass_, bar * 4 + 2, 0, 2, exploreRoots[bar] + 4, 90);
        }

        const int leadA[32][2] = {
            {4, 4}, {0, -2}, {4, 2}, {0, -2}, {4, 4}, {4, 5}, {4, 4}, {0, -2},
            {4, 2}, {0, -2}, {4, 0}, {0, -2}, {3, 5}, {0, -2}, {0, -1}, {0, -2},
            {4, 3}, {0, -2}, {4, 5}, {0, -2}, {5, 0}, {0, -2}, {4, 5}, {4, 3},
            {4, 4}, {0, -2}, {0, -2}, {0, -2}, {4, 1}, {0, -2}, {3, 6}, {0, -1},
        };  // {octave, degree}; degree -1 = off, -2 = empty
        for (int r = 0; r < 32; ++r) {
            if (leadA[r][1] != -2) put(exploreLead_, r, 0, leadA[r][0], leadA[r][1], 100);
        }

        for (int bar = 0; bar < 4; ++bar) {
            for (int k = 0; k < 16; k += 2) {
                exploreArp_.cells[bar * 16 + k][0] =
                    Cell::degree(5, uint8_t(exploreRoots[bar]), 0, 50).with(fx::kArpeggio, 0x24);
            }
        }

        // ---- Combat (128 BPM, 4 bars) ----
        chordPattern(combatPad_, combatRoots, 4);

        for (int bar = 0; bar < 4; ++bar) {
            for (int k = 0; k < 16; k += 2) {
                put(combatBass_, bar * 16 + k, 0, (k % 4 == 0) ? 2 : 3, combatRoots[bar], k % 4 == 0 ? 127 : 80);
            }
        }

        const int motif[6][2] = {{0, 4}, {3, 2}, {6, 4}, {8, 7}, {11, 6}, {14, 4}};  // {row, degree above root}
        for (int bar = 0; bar < 4; ++bar) {
            for (const auto& m : motif) {
                put(combatLead_, bar * 16 + m[0], 0, 4, combatRoots[bar] + m[1], 110);
                if (m[0] + 2 < 16) put(combatLead_, bar * 16 + m[0] + 2, 0, 0, -1);
            }
        }

        for (int bar = 0; bar < 4; ++bar) {
            for (int k = 0; k < 16; ++k) {
                combatArp_.cells[bar * 16 + k][0] =
                    Cell::degree(5, uint8_t(combatRoots[bar]), 0, k % 4 == 0 ? 70 : 45).with(fx::kArpeggio, 0x24);
            }
        }

        for (int beat = 0; beat < 4; ++beat) {
            combatDrums_.cells[beat * 4][0] = Cell::on(45, 127).with(fx::kPortaDown, 0x60);
            combatDrums_.cells[beat * 4 + 2][1] = Cell::on(108, 60);
        }

        // ---- Bridge (1 bar riser into combat) ----
        put(bridgeBass_, 0, 0, 2, 4, 127);  // the V chord's root, held

        for (int k = 0; k < 16; ++k) {
            bridgeArp_.cells[k][0] = Cell::degree(uint8_t(4 + k / 8), uint8_t(k % 8), 0, uint8_t(50 + k * 4));
        }

        for (int k = 0; k < 16; ++k) {
            if (k < 8 ? (k % 4 == 0) : (k >= 12 || k % 2 == 0)) {
                bridgeDrums_.cells[k][0] = Cell::on(45, uint8_t(80 + k * 3)).with(fx::kPortaDown, 0x60);
            }
        }

        // ---- Stingers ----
        hit_.cells[0][0] = Cell::degree(5, 4, 0, 127).with(fx::kPortaDown, 0x18);
        hit_.cells[5][0] = Cell::off();

        put(pickup_, 0, 0, 5, 0, 110);
        put(pickup_, 1, 0, 5, 2, 110);
        put(pickup_, 2, 0, 5, 4, 110);
        put(pickup_, 3, 0, 6, 0, 120);
        put(pickup_, 5, 0, 0, -1);

        // ---- Sections: what each channel plays ----
        auto one = [](Section& s, uint8_t channel, uint8_t pattern) { s.sequences[channel].append(pattern); };
        Section& explore = song_.sections[kExplore];
        one(explore, kLead, kExploreLead);
        one(explore, kBass, kExploreBass);
        one(explore, kPad, kExplorePad);
        one(explore, kArp, kExploreArp);
        explore.tempoMilliBpm = 96000;

        Section& combat = song_.sections[kCombat];
        one(combat, kLead, kCombatLead);
        one(combat, kBass, kCombatBass);
        one(combat, kPad, kCombatPad);
        one(combat, kArp, kCombatArp);
        one(combat, kDrums, kCombatDrums);
        combat.tempoMilliBpm = 128000;

        Section& bridge = song_.sections[kBridge];
        one(bridge, kBass, kBridgeBass);
        one(bridge, kArp, kBridgeArp);
        one(bridge, kDrums, kBridgeDrums);
        bridge.tempoMilliBpm = 128000;
        bridge.bridgeBeats = 4;
        song_.numSections = 3;

        // ---- Layers ----
        song_.layers[kRhythmLayer] = layerChannelBits(kArp) | layerChannelBits(kDrums);
        song_.layers[kMelodyLayer] = layerChannelBits(kLead);
        song_.layers[kPadLayer] = layerChannelBits(kPad);
        song_.numLayers = 3;

        // ---- Events ----
        song_.events[kHit].add(Command::playStinger(kStinger, kHitStinger, Quantize::Now));
        song_.events[kPickup].add(Command::playStinger(kStinger, kPickupStinger, Quantize::Beat));
        song_.events[kEnterCombat].add(Command::gotoSection(kCombat, Quantize::Bar, Entry::Restart, kBridge));
        song_.events[kCalmDown].add(Command::gotoSection(kExplore, Quantize::Bar, Entry::KeepPhase));
        song_.events[kAlarm].add(Command::gotoSection(kCombat, Quantize::Now, Entry::Restart));
        song_.events[kAlarm].add(Command::playStinger(kStinger, kHitStinger, Quantize::Now));
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

    void buildInstruments() {
        using P = Instrument::Param;
        auto env = [](Instrument& i, float a, float d, float s, float r) {
            i.set(P::AmpAttack, a).set(P::AmpDecay, d).set(P::AmpSustain, s).set(P::AmpRelease, r);
        };
        Instrument lead, bass, pad, arp, kick, hat, stinger;
        lead.set(P::Shape, 0.5f).set(P::Cutoff, 2200.0f).set(P::VibratoDepth, 0.05f).set(P::Level, 0.28f);
        env(lead, 0.005f, 0.25f, 0.5f, 0.12f);
        bass.set(P::Shape, 0.0f).set(P::Cutoff, 320.0f).set(P::Resonance, 3.0f).set(P::FilterEnvAmount, 0.5f)
            .set(P::Level, 0.42f);
        env(bass, 0.002f, 0.2f, 0.6f, 0.06f);
        pad.set(P::Shape, 0.3f).set(P::Cutoff, 1000.0f).set(P::VibratoDepth, 0.06f).set(P::Level, 0.1f);
        env(pad, 0.25f, 0.5f, 0.7f, 0.6f);
        arp.set(P::Shape, 1.0f).set(P::Cutoff, 4000.0f).set(P::Level, 0.1f);
        env(arp, 0.001f, 0.09f, 0.15f, 0.05f);
        kick.set(P::Shape, 0.5f).set(P::Cutoff, 900.0f).set(P::Level, 0.85f);
        env(kick, 0.001f, 0.18f, 0.0f, 0.05f);
        hat.set(P::Shape, 1.0f).set(P::Cutoff, 12000.0f).set(P::Level, 0.12f);
        env(hat, 0.0005f, 0.03f, 0.0f, 0.02f);
        stinger.set(P::Shape, 0.9f).set(P::Cutoff, 5000.0f).set(P::Resonance, 2.0f).set(P::Level, 0.3f);
        env(stinger, 0.001f, 0.15f, 0.4f, 0.15f);
        for (const Instrument* i : {&lead, &bass, &pad, &arp, &kick, &hat, &stinger}) song_.addInstrument(*i);

        song_.assign(kLead, kLeadSynth);
        song_.assign(kBass, kBassSynth);
        song_.assign(kPad, kPadSynth);
        song_.assign(kArp, kArpSynth);
        song_.assign(kDrums, kKick);
        song_.channels[kDrums].instruments[1] = kHat;
        song_.assign(kStinger, kStingerSynth);
    }

    Song song_;
};

} // namespace adaptive::demo
