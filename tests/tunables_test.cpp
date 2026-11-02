// tunables_test - the ADVANCED tab's table: ranges and defaults are consistent everywhere, Set clamps/quantizes, Reset restores, the settings file
// round-trips them, and the defaults reproduce the values that were compile-time constants before.
#include "ConfigSchema.h"
#include "EspConfig.h"
#include "Presets.h"
#include "Tunables.h"
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace tunables;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

int main()
{
    // ---- the table itself ----
    for (int i = 0; i < Count(); ++i)
    {
        const Def& d = Get(static_cast<Id>(i));
        CHECK(d.id == static_cast<Id>(i));
        CHECK(d.min < d.max && d.step > 0 && d.def >= d.min && d.def <= d.max);
        CHECK(std::fabs(Quantize(d, d.def) - d.def) < 1e-9);          // the default sits on the step grid
        CHECK(Quantize(d, d.min) == d.min);
        CHECK(Find(d.key) == &d);
        CHECK(d.help && *d.help && d.label && *d.label && d.unit);
        CHECK(std::strlen(d.help) <= 72 && std::strlen(d.label) <= 22); // must fit the 480 px window on one line
        // every key is a key of the settings file (a spec in the schema, or MaxLabelDist's own)
        CHECK(configschema::IsKnownKey(d.key));
        // ... and a range in the schema agrees with the table (the schema reads this very table)
        configschema::Range r;
        if (!d.maxIsUnlimited) CHECK(configschema::NumericRange(d.key, r) && r.min == d.min && r.max == d.max);
    }
    CHECK(Find("NoSuchKey") == nullptr);

    // ---- defaults == the constants of before (so nothing changed for anyone) ----
    {
        Settings s;
        const SmootherTuning t = ToSmoother(s);
        CHECK(t == SmootherTuning{});
        CHECK(t.baseline == 0.012 && t.blendTau == 0.010 && t.maxAhead == 0.05 && t.tick == 0.05);
        CHECK(s.agentHz == 120 && s.itemMinPx == 6.f && s.playerMinPx == 2.f && s.labelDist == presets::kUnlimited);
        for (int i = 0; i < Count(); ++i) CHECK(IsDefault(s, static_cast<Id>(i)));
        EspConfig c;
        CHECK(c.SmootherBaselineMs == 12 && c.SmootherBlendTauMs == 10 && c.AgentHz == 120 && c.ItemMinPx == 6 && c.PlayerMinPx == 2);
    }

    // ---- Set: clamp, quantize, NaN; Reset ----
    {
        Settings s;
        CHECK(Set(s, Id::SmootherBlendTauMs, 999) == 50 && s.smBlendTauMs == 50);
        CHECK(Set(s, Id::SmootherBlendTauMs, -4) == 0);
        CHECK(Set(s, Id::SmootherMaxAheadMs, 47) == 45);                 // step 5
        CHECK(Set(s, Id::AgentHz, 1000) == 240 && s.agentHz == 240);
        CHECK(Set(s, Id::AgentHz, 0) == 10 && s.agentHz == 10);
        CHECK(Set(s, Id::ItemMinPx, std::nan("")) == 6);                 // not a number -> the default
        CHECK(Set(s, Id::PlayerMinPx, 0) == 0 && s.playerMinPx == 0);   // 0 is allowed: true size
        CHECK(!IsDefault(s, Id::AgentHz));
        for (int i = 0; i < Count(); ++i) { Reset(s, static_cast<Id>(i)); CHECK(IsDefault(s, static_cast<Id>(i))); }
        CHECK(ToSmoother(s) == SmootherTuning{} && s.labelDist == presets::kUnlimited);
    }

    // ---- label distance: the top of the slider is "Unlimited" ----
    {
        Settings s;
        CHECK(Set(s, Id::LabelDist, 75) == 70 || Set(s, Id::LabelDist, 75) == 80);   // step 10
        CHECK(s.labelDist < 1000.f);
        Set(s, Id::LabelDist, 510);
        CHECK(s.labelDist == presets::kUnlimited && Format(Get(Id::LabelDist), Value(s, Id::LabelDist)) == "Unlimited");
        Set(s, Id::LabelDist, 100);
        CHECK(s.labelDist == 100.f && Format(Get(Id::LabelDist), 100) == "100 m");
        // the file stores a plain distance (-1 = unlimited) as before
        CHECK(s.Build().MaxLabelDist == 100);
        Set(s, Id::LabelDist, 510);
        CHECK(s.Build().MaxLabelDist == -1);
    }

    // ---- the file round trip, incl. a value the old preset cycle could not hold (130 m) ----
    {
        Settings s;
        Set(s, Id::SmootherBaselineMs, 20);
        Set(s, Id::SmootherTickMs, 75);
        Set(s, Id::AgentHz, 90);
        Set(s, Id::ItemMinPx, 10);
        Set(s, Id::PlayerMinPx, 4);
        Set(s, Id::LabelDist, 130);
        const std::string text = s.Build().ToJson();
        const auto r = configschema::LoadText(text);
        CHECK(r.Warnings() == 0);
        Settings t;
        t.Apply(r.cfg);
        for (int i = 0; i < Count(); ++i) CHECK(Value(t, static_cast<Id>(i)) == Value(s, static_cast<Id>(i)));
        CHECK(t.labelDist == 130.f);
    }

    // ---- an old file (without the keys) gets the defaults, a bad one gets a message ----
    {
        const auto old = configschema::LoadText(R"({"Version":1,"BoxStyle":"Sharp"})");
        CHECK(old.Warnings() == 0 && old.cfg.AgentHz == 120 && old.cfg.SmootherTickMs == 50);
        const auto bad = configschema::LoadText(R"({"AgentHz":9000,"SmootherTickMs":"fast","ItemMinPx":-3,"PlayerMinPx":99})");
        CHECK(bad.Warnings() == 4 && bad.cfg.AgentHz == 240 && bad.cfg.SmootherTickMs == 50 && bad.cfg.ItemMinPx == 2 && bad.cfg.PlayerMinPx == 12);
    }

    // ---- the live readout: degrees on the screen ----
    {
        CHECK(std::fabs(AngleToPx(1.0, 1080, 70.0) - 13.46) < 0.05);          // 1 deg at 1080p / 70 deg FOV
        CHECK(std::fabs(AngleToPx(1.0, 2160, 70.0) - 26.92) < 0.1);           // twice the pixels at 4K
        CHECK(AngleToPx(0.0, 1080, 70.0) == 0.0);
        CHECK(AngleToPx(1.0, 0, 70.0) == 0.0 && AngleToPx(1.0, 1080, 0.0) == 0.0 && AngleToPx(std::nan(""), 1080, 70.0) == 0.0);
        SmootherStats st;
        CHECK(DescribeError(st, 1080, 70).find("no camera data") != std::string::npos);
        st.samples = 50; st.camAngErrDeg = 0.31; st.camPosErr = 0.003;
        const std::string line = DescribeError(st, 1080, 70);
        CHECK(line == "Prediction error: 0.31 deg = 4.2 px, 0.0030 blocks");
        CHECK(line.size() <= 72);
    }

    if (fails) std::printf("%d failure(s)\n", fails); else std::puts("tunables_test: all passed");
    return fails ? 1 : 0;
}
