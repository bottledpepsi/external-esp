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
             "Age of the sample speed is measured from. Up = steadier, slower.", false},
            {Id::SmootherBlendTauMs, "SmootherBlendTauMs", "Correction blend", "ms", 0, 50, SMOOTHER_BLEND_TAU * 1000.0, 1, 0,
             "How long each jump is smeared out. 0 = snaps; up = softer, lags.", false},
            {Id::SmootherMaxAheadMs, "SmootherMaxAheadMs", "Max extrapolation", "ms", 0, 100, 50, 5, 0,
             "How far past the newest sample the pose is guessed. 0 = never.", false},
            {Id::SmootherTickMs, "SmootherTickMs", "Game tick", "ms", 25, 100, 50, 5, 0,
             "Game tick length for other players. 50 ms = 20 TPS (normal).", false},
            {Id::AgentHz, "AgentHz", "Agent rate", "Hz", 10, 240, 120, 5, 0,
             "Samples per second from the game. Up = smoother, more game CPU.", false},
            {Id::ItemMinPx, "ItemMinPx", "Item marker size", "px", 2, 24, 6, 1, 0,
             "Smallest size an item is drawn at; keeps far items visible.", false},
            {Id::PlayerMinPx, "PlayerMinPx", "Player box minimum", "px", 0, 12, 2, 1, 0,
             "Smallest player box (far players grow to it). 0 = true size.", false},
            {Id::LabelDist, "MaxLabelDist", "Label distance", "m", 10, 510, 510, 10, 0,
             "Names and distances hide beyond this. Far right = Unlimited.", true},
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

    double AngleToPx(double deg, int heightPx, double fovYDeg)
    {
        if (heightPx <= 0 || !(fovYDeg > 1.0 && fovYDeg < 179.0) || !std::isfinite(deg)) return 0.0;
        const double rad = 3.14159265358979323846 / 180.0;
        return std::tan(deg * rad) * (heightPx * 0.5) / std::tan(fovYDeg * rad * 0.5);
    }

    std::string DescribeError(const SmootherStats& st, int heightPx, double fovYDeg)
    {
        if (st.samples < 5) return "Prediction error: no camera data yet (move the view).";
        char b[160];
        std::snprintf(b, sizeof b, "Prediction error: %.2f deg = %.1f px, %.4f blocks", st.camAngErrDeg, AngleToPx(st.camAngErrDeg, heightPx, fovYDeg),
                      st.camPosErr);
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
