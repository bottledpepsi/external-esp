// dpi_format_test.cpp - the text the log shows about monitors (src/DpiFormat.h).
#include "../src/DpiFormat.h"
#include <cstdio>

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

int main()
{
    CHECK(dpi::ScalePercent(96) == 100 && dpi::ScalePercent(120) == 125 && dpi::ScalePercent(144) == 150 && dpi::ScalePercent(168) == 175 && dpi::ScalePercent(192) == 200);
    CHECK(dpi::ScalePercent(288) == 300 && dpi::ScalePercent(0) == 0);
    CHECK(dpi::MonitorLine(1, 2560, 1440, 0, 0, 144, true) == "monitor 1: 2560x1440 at (0,0), 150% (144 dpi), primary");
    CHECK(dpi::MonitorLine(2, 1920, 1080, -1920, 120, 96, false) == "monitor 2: 1920x1080 at (-1920,120), 100% (96 dpi)");
    CHECK(dpi::MonitorLine(3, 800, 600, 0, 0, 0, false) == "monitor 3: 800x600 at (0,0), dpi unknown");
    std::printf(g_fail == 0 ? "ALL OK (%d checks)\n" : "%d FAILURES\n", g_fail == 0 ? g_checks : g_fail);
    return g_fail == 0 ? 0 : 1;
}
