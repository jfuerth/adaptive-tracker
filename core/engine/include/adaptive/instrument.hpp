#pragma once

#include <sawtooth_synth.hpp>

#include <cstdint>

namespace adaptive {

/**
 * A synth patch as plain data: one float per parameter, in natural units
 * (seconds, Hz, 0..1). Defaults are read from a default pressense voice, so
 * they can't drift from the synth's own. Songs hold a table of these; each column of each
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

    /// Valid range of each parameter: the editor's slider span. Where
    /// pressense defines a range it is used; the rest are editor choices.
    struct Range {
        float min, max;
    };
    static constexpr Range range(Param p) {
        constexpr float kTimeMin = 0.0005f, kTimeMax = 10.0f;
        using S = synth::WavetableSynth;
        switch (p) {
        case Param::Shape: return {0.0f, 1.0f};
        case Param::Cutoff: return {20.0f, 20000.0f};
        case Param::Resonance: return {S::FILTER_Q_MIN, S::FILTER_Q_MAX};
        case Param::FilterEnvAmount: return {0.0f, 1.0f};
        case Param::FilterAttack:
        case Param::FilterDecay:
        case Param::FilterRelease:
        case Param::AmpAttack:
        case Param::AmpDecay:
        case Param::AmpRelease: return {kTimeMin, kTimeMax};
        case Param::FilterSustain:
        case Param::AmpSustain: return {0.0f, 1.0f};
        case Param::Level: return {S::LEVEL_MIN, S::LEVEL_MAX};
        case Param::VibratoRate:
        case Param::TremoloRate: return {S::LFO_RATE_MIN, S::LFO_RATE_MAX};
        case Param::VibratoDepth: return {0.0f, 2.0f};
        case Param::TremoloDepth: return {0.0f, 1.0f};
        case Param::Count: break;
        }
        return {0.0f, 0.0f};
    }

    /// Stable name of each parameter, so the app can match parameters by name
    /// rather than by position.
    static constexpr const char* key(Param p) {
        switch (p) {
        case Param::Shape: return "shape";
        case Param::Cutoff: return "cutoff";
        case Param::Resonance: return "resonance";
        case Param::FilterEnvAmount: return "filterEnvAmount";
        case Param::FilterAttack: return "filterAttack";
        case Param::FilterDecay: return "filterDecay";
        case Param::FilterSustain: return "filterSustain";
        case Param::FilterRelease: return "filterRelease";
        case Param::AmpAttack: return "ampAttack";
        case Param::AmpDecay: return "ampDecay";
        case Param::AmpSustain: return "ampSustain";
        case Param::AmpRelease: return "ampRelease";
        case Param::Level: return "level";
        case Param::VibratoRate: return "vibratoRate";
        case Param::VibratoDepth: return "vibratoDepth";
        case Param::TremoloRate: return "tremoloRate";
        case Param::TremoloDepth: return "tremoloDepth";
        case Param::Count: break;
        }
        return "";
    }

    float values[kNumParams];

    /// pressense's own defaults: whatever a freshly constructed voice has.
    Instrument() { *this = defaults(); }

    /// The settings of `v`, read back through its getters.
    static Instrument capture(synth::WavetableSynth& v) {
        Instrument i{Blank{}};
        i.set(Param::Shape, v.getWaveformShape())
            .set(Param::Cutoff, v.getBaseCutoff())
            .set(Param::Resonance, v.getFilterQ())
            .set(Param::FilterEnvAmount, v.getFilterEnvelopeAmount())
            .set(Param::FilterAttack, v.getFilterEnvelope().getAttackTime())
            .set(Param::FilterDecay, v.getFilterEnvelope().getDecayTime())
            .set(Param::FilterSustain, v.getFilterEnvSustain())
            .set(Param::FilterRelease, v.getFilterEnvelope().getReleaseTime())
            .set(Param::AmpAttack, v.getAmpEnvelope().getAttackTime())
            .set(Param::AmpDecay, v.getAmpEnvelope().getDecayTime())
            .set(Param::AmpSustain, v.getAmpEnvelope().getSustainLevel())
            .set(Param::AmpRelease, v.getAmpEnvelope().getReleaseTime())
            .set(Param::Level, v.getLevel())
            .set(Param::VibratoRate, v.getVibratoRate())
            .set(Param::VibratoDepth, v.getVibratoDepth())
            .set(Param::TremoloRate, v.getTremoloRate())
            .set(Param::TremoloDepth, v.getTremoloDepth());
        return i;
    }

    /// Captured once from a default-constructed voice.
    static const Instrument& defaults() {
        static const Instrument d = [] {
            synth::WavetableSynth v;
            return capture(v);
        }();
        return d;
    }

    float get(Param p) const { return values[static_cast<uint8_t>(p)]; }
    /// Set a parameter, clamped to its range. Returns *this for chaining.
    Instrument& set(Param p, float v) {
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

private:
    struct Blank {};
    explicit Instrument(Blank) : values{} {}
};

} // namespace adaptive
