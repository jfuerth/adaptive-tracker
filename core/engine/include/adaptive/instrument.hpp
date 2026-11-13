#pragma once

#include <sawtooth_synth.hpp>

#include <cstdint>

namespace adaptive {

/**
 * A synth patch as plain data: one float per parameter, in natural units
 * (seconds, Hz, 0..1). Songs hold a table of these; each column of each
 * channel names the one its voice uses. Applying one sets every parameter of
 * a pressense voice, so a voice's sound depends only on its instrument (plus
 * whatever pattern effects change while it plays).
 */
struct Instrument {
    enum class Param : uint8_t {
        Shape,            ///< 0 = saw, 0.5 = triangle, 1 = square
        Cutoff,           ///< Hz
        Resonance,        ///< filter Q
        FilterEnvAmount,  ///< 0..1: how far the filter envelope opens the cutoff
        FilterAttack,     ///< s
        FilterDecay,      ///< s
        FilterSustain,    ///< 0..1
        FilterRelease,    ///< s
        AmpAttack,        ///< s
        AmpDecay,         ///< s
        AmpSustain,       ///< 0..1
        AmpRelease,       ///< s
        Level,            ///< 0..1, before the shared clipper
        VibratoRate,      ///< Hz
        VibratoDepth,     ///< semitones
        TremoloRate,      ///< Hz
        TremoloDepth,     ///< 0..1
        Count,
    };
    static constexpr uint8_t kNumParams = static_cast<uint8_t>(Param::Count);

    struct Range {
        float min, max, defaultValue;
    };
    /// Valid range and default of each parameter. Defaults are pressense's own.
    static constexpr Range range(Param p) {
        constexpr float kTimeMin = 0.0005f, kTimeMax = 10.0f;
        switch (p) {
        case Param::Shape: return {0.0f, 1.0f, 0.0f};
        case Param::Cutoff: return {20.0f, 20000.0f, 1000.0f};
        case Param::Resonance:
            return {synth::WavetableSynth::FILTER_Q_MIN, synth::WavetableSynth::FILTER_Q_MAX, 0.707f};
        case Param::FilterEnvAmount: return {0.0f, 1.0f, 0.5f};
        case Param::FilterAttack: return {kTimeMin, kTimeMax, 0.005f};
        case Param::FilterDecay: return {kTimeMin, kTimeMax, 0.2f};
        case Param::FilterSustain: return {0.0f, 1.0f, 0.3f};
        case Param::FilterRelease: return {kTimeMin, kTimeMax, 0.1f};
        case Param::AmpAttack: return {kTimeMin, kTimeMax, 0.01f};
        case Param::AmpDecay: return {kTimeMin, kTimeMax, 0.05f};
        case Param::AmpSustain: return {0.0f, 1.0f, 0.7f};
        case Param::AmpRelease: return {kTimeMin, kTimeMax, 0.1f};
        case Param::Level: return {0.0f, 1.0f, 0.5f};
        case Param::VibratoRate:
            return {synth::WavetableSynth::LFO_RATE_MIN, synth::WavetableSynth::LFO_RATE_MAX, 5.0f};
        case Param::VibratoDepth: return {0.0f, 2.0f, 0.0f};
        case Param::TremoloRate:
            return {synth::WavetableSynth::LFO_RATE_MIN, synth::WavetableSynth::LFO_RATE_MAX, 5.0f};
        case Param::TremoloDepth: return {0.0f, 1.0f, 0.0f};
        case Param::Count: break;
        }
        return {0.0f, 0.0f, 0.0f};
    }

    float values[kNumParams];

    constexpr Instrument() : values{} {
        for (uint8_t i = 0; i < kNumParams; ++i) values[i] = range(static_cast<Param>(i)).defaultValue;
    }

    constexpr float get(Param p) const { return values[static_cast<uint8_t>(p)]; }
    /// Set a parameter, clamped to its range. Returns *this for chaining.
    constexpr Instrument& set(Param p, float v) {
        const Range r = range(p);
        values[static_cast<uint8_t>(p)] = v < r.min ? r.min : (v > r.max ? r.max : v);
        return *this;
    }

    /// Apply every parameter to a voice.
    void applyTo(synth::WavetableSynth& v) const {
        for (uint8_t i = 0; i < kNumParams; ++i) applyParam(v, static_cast<Param>(i));
    }

    /// Apply one parameter to a voice (e.g. after editing it).
    void applyParam(synth::WavetableSynth& v, Param p) const {
        const float x = get(p);
        switch (p) {
        case Param::Shape: v.setWaveformShape(x); break;
        case Param::Cutoff: v.setBaseCutoff(x); break;
        case Param::Resonance: v.setFilterQ(x); break;
        case Param::FilterEnvAmount: v.setFilterEnvelopeAmount(x); break;
        case Param::FilterAttack: v.getFilterEnvelope().setAttackTime(x); break;
        case Param::FilterDecay: v.getFilterEnvelope().setDecayTime(x); break;
        case Param::FilterSustain: v.setFilterEnvSustain(x); break;
        case Param::FilterRelease: v.getFilterEnvelope().setReleaseTime(x); break;
        case Param::AmpAttack: v.getAmpEnvelope().setAttackTime(x); break;
        case Param::AmpDecay: v.getAmpEnvelope().setDecayTime(x); break;
        case Param::AmpSustain: v.getAmpEnvelope().setSustainLevel(x); break;
        case Param::AmpRelease: v.getAmpEnvelope().setReleaseTime(x); break;
        case Param::Level: v.setLevel(x); break;
        case Param::VibratoRate: v.setVibratoRate(x); break;
        case Param::VibratoDepth: v.setVibratoDepth(x); break;
        case Param::TremoloRate: v.setTremoloRate(x); break;
        case Param::TremoloDepth: v.setTremoloDepth(x); break;
        case Param::Count: break;
        }
    }
};

} // namespace adaptive
