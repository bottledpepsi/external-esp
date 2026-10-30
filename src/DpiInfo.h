// DpiInfo.h - DPI awareness of this process and a description of the monitors, for the log. Windows-only. All APIs are loaded dynamically so the exe still
// starts on Windows 7/8; what was available is part of the returned text.
#pragma once
#include <string>
#include <windows.h>

namespace dpi
{
    // Makes the process per-monitor-v2 DPI aware, else per-monitor, else system aware. Must run before any window exists. Returns what was achieved,
    // e.g. "per-monitor v2 (SetProcessDpiAwarenessContext)". Why per-monitor: a system-aware process sees coordinates VIRTUALISED for every monitor whose scale
    // differs from the primary's, so the overlay would be offset or the wrong size over a game on a second monitor with another scale.
    std::string Enable();

    // One line per monitor: "monitor 1: 2560x1440 at (0,0), 150% (144 dpi), primary" - for the log at start-up and after a display change.
    std::string DescribeMonitors();

    // The DPI awareness of another window: "per-monitor v2", "per-monitor", "system", "unaware" or "unknown" (older Windows).
    std::string WindowAwareness(HWND hwnd);

    // "150% (144 dpi)" style text; pure, tested in tests/dpi_format_test.cpp through DpiInfoFormat.h.
} // namespace dpi
