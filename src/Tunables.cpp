#include "Tunables.h"
#include "Presets.h"
#include <cmath>
#include <cstdio>

namespace tunables
{
    namespace
    {
        // Defaults for the four smoother values are the ones the golden recordings were tuned with (Smoother.h); see docs/tuning.md.
        const Def kTable[] = {
            {Id::SmootherBaselineMs, "SmootherBaselineMs", "Velocity baseline", "ms", 2, 50, SMOOTHER_BASELINE * 1000.0, 1, 0,
             "How old the sample is that speed is measured against. Higher = steadier speed, slower to react.", false},
            {Id::SmootherBlendTauMs, "SmootherBlendTauMs", "Correction blend", "ms", 0, 50, SMOOTHER_BLEND_TAU * 1000.0, 1, 0,
             "How long the jump to each new sample is smeared out. 0 = the picture snaps; higher = softer, but lags more.", false},
            {Id::SmootherMaxAheadMs, "SmootherMaxAheadMs", "Max extrapolation", "ms", 0, 100, 50, 5, 0,
             "How far past the newest sample the pose may be guessed. 0 = never guess; higher = smoother at a low sample rate.", false},
            {Id::SmootherTickMs, "SmootherTickMs", "Game tick", "ms", 25, 100, 50, 5, 0,
             "Length of one game tick for player movement. 50 = 20 TPS (normal); raise it only for a server that runs slow.", false},
            {Id::AgentHz, "AgentHz", "Agent rate", "Hz", 10, 240, 120, 5, 0,
             "Samples per second the game agent sends. Higher = smoother, a little more CPU in the game. The command line --hz overrides it for one run.", false},
            {Id::ItemMinPx, "ItemMinPx", "Item marker size", "px", 2, 24, 6, 1, 0,
             "Smallest size an item is drawn at, so far-away items stay visible instead of disappearing.", false},
            {Id::PlayerMinPx, "PlayerMinPx", "Player box minimum", "px", 0, 12, 2, 1, 0,
             "Smallest width/height of a player box. Far players are drawn at this size instead of vanishing. 0 = true size.", false},
            {Id::LabelDist, "MaxLabelDist", "Label distance", "m", 10, 510, 510, 10, 0,
             "Names and distances are hidden beyond this. The top of the slider is Unlimited.", true},
        };
        static_assert(sizeof kTable / sizeof kTable[0] == static_cast<size_t>(Id::Count), "one Def per Id");
    } // namespace

    const Def& Get(Id id) { return kTable[static_cast<size_t>(id)]; }
    int Count() { return static_cast<int>(Id::Count); }

    const Def* Find(const char* key)
    {
        for (const Def& d : kTable) if (std::string(d.key) == key) return &d;
        return nullptr;
    }

    double Quantize(const Def& d, double v)
    {
        if (!std::isfinite(v)) v = d.def;
        v = std::clamp(v, d.min, d.max);
        const double q = d.min + std::round((v - d.min) / d.step) * d.step; // steps count from the minimum
        return std::clamp(q, d.min, d.max);
    }

    double Value(const Settings& s, Id id)
    {
        switch (id)
        {
        case Id::SmootherBaselineMs: return s.smBaselineMs;
        case Id::SmootherBlendTauMs: return s.smBlendTauMs;
        case Id::SmootherMaxAheadMs: return s.smMaxAheadMs;
        case Id::SmootherTickMs: return s.smTickMs;
        case Id::AgentHz: return s.agentHz;
        case Id::ItemMinPx: return s.itemMinPx;
        case Id::PlayerMinPx: return s.playerMinPx;
        case Id::LabelDist: return s.labelDist >= presets::kUnlimited ? Get(Id::LabelDist).max : s.labelDist;
        default: return 0;
        }
    }

    double Set(Settings& s, Id id, double v)
    {
        const Def& d = Get(id);
        const double q = Quantize(d, v);
        switch (id)
        {
        case Id::SmootherBaselineMs: s.smBaselineMs = q; break;
        case Id::SmootherBlendTauMs: s.smBlendTauMs = q; break;
        case Id::SmootherMaxAheadMs: s.smMaxAheadMs = q; break;
        case Id::SmootherTickMs: s.smTickMs = q; break;
        case Id::AgentHz: s.agentHz = static_cast<int>(q); break;
        case Id::ItemMinPx: s.itemMinPx = static_cast<float>(q); break;
        case Id::PlayerMinPx: s.playerMinPx = static_cast<float>(q); break;
        case Id::LabelDist: s.labelDist = q >= d.max ? presets::kUnlimited : static_cast<float>(q); break;
        default: break;
        }
        return q;
    }

    double Reset(Settings& s, Id id) { return Set(s, id, Get(id).def); }
    bool IsDefault(const Settings& s, Id id) { return std::fabs(Value(s, id) - Get(id).def) < 1e-9; }

    std::string Format(const Def& d, double v)
    {
        if (d.maxIsUnlimited && v >= d.max) return "Unlimited";
        char b[48];
        std::snprintf(b, sizeof b, "%.*f %s", d.decimals, v, d.unit);
        return b;
    }

    SmootherTuning ToSmoother(const Settings& s)
    {
        SmootherTuning t;
        t.baseline = s.smBaselineMs / 1000.0;
        t.blendTau = s.smBlendTauMs / 1000.0;
        t.maxAhead = s.smMaxAheadMs / 1000.0;
        t.tick = s.smTickMs / 1000.0;
        return t.Sanitized();
    }
} // namespace tunables
