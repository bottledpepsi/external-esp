// Tunables.h - the numbers a user may tune in the ADVANCED tab, in ONE table: key, label, unit, range, step, default, one-line help.
// The settings file's validation (ConfigSchema), the UI and the tests all read this table, so a range or default is written once.
// Portable (no Windows headers).
#pragma once
#include "EspConfig.h"
#include "Smoother.h"

namespace tunables
{
    enum class Id
    {
        SmootherBaselineMs,
        SmootherBlendTauMs,
        SmootherMaxAheadMs,
        SmootherTickMs,
        AgentHz,
        ItemMinPx,
        PlayerMinPx,
        LabelDist,
        Count
    };

    struct Def
    {
        Id id;
        const char* key; // the key in esp_config.json
        const char* label; // shown in the UI
        const char* unit; // "ms", "px", "Hz", "m"
        double min, max, def, step;
        int decimals; // digits shown after the point
        const char* help; // one line: what it does and what moving it up does
        bool maxIsUnlimited; // the top of the range means "no limit" (shown as "Unlimited", stored as -1 in the file)
    };

    const Def& Get(Id id);
    const Def* Find(const char* key); // by config key, nullptr if it is not a tunable
    int Count();

    double Value(const Settings& s, Id id); // the current value, in the table's unit (LabelDist: meters, max = unlimited)
    // Clamps to the range, rounds to the step, and stores. Returns the value stored.
    double Set(Settings& s, Id id, double v);
    double Reset(Settings& s, Id id); // back to the default
    bool IsDefault(const Settings& s, Id id);
    double Quantize(const Def& d, double v);

    std::string Format(const Def& d, double v); // "12 ms", "Unlimited", "2.5 px"

    SmootherTuning ToSmoother(const Settings& s); // for Smoother::SetTuning

    // ---- live readout (ADVANCED tab) ----
    // An angle on the screen, in pixels: the window is `heightPx` tall and shows `fovYDeg` degrees vertically (the pinhole model the overlay itself
    // uses: pixels per radian at the screen centre = (h/2) / tan(fov/2)).
    double AngleToPx(double deg, int heightPx, double fovYDeg);
    // Two short lines for the tab: how far each new camera sample was from the prediction, in degrees and pixels, and what to do with the number.
    std::string DescribeError(const SmootherStats& st, int heightPx, double fovYDeg);
} // namespace tunables
