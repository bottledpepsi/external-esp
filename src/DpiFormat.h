// DpiFormat.h - pure text helpers for DpiInfo (no Windows headers, so tests/ can run them anywhere).
#pragma once
#include <string>

namespace dpi
{
    inline int ScalePercent(unsigned dpi) { return static_cast<int>((dpi * 100u + 48u) / 96u); } // 96 -> 100, 120 -> 125, 144 -> 150, 192 -> 200

    inline std::string MonitorLine(int index, int w, int h, int x, int y, unsigned dpi, bool primary)
    {
        std::string s = "monitor " + std::to_string(index) + ": " + std::to_string(w) + "x" + std::to_string(h) + " at (" + std::to_string(x) + "," + std::to_string(y) + "), ";
        s += dpi ? std::to_string(ScalePercent(dpi)) + "% (" + std::to_string(dpi) + " dpi)" : std::string("dpi unknown");
        if (primary) s += ", primary";
        return s;
    }
} // namespace dpi
