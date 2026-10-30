// window_state_test.cpp - classification of the game window and the raise decision (src/WindowState.h).
#include "../src/WindowState.h"
#include <cstdio>

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

using namespace winstate;
using namespace std::chrono_literals;

static Facts Base()
{
    Facts f;
    f.monitor = {0, 0, 2560, 1440};
    f.client = {100, 130, 1380, 850};   // an ordinary window
    f.hasCaption = true;
    return f;
}

int main()
{
    { Facts f = Base(); CHECK(Classify(f) == Kind::Windowed); }
    { Facts f = Base(); f.client = {0, 31, 2560, 1440}; CHECK(Classify(f) == Kind::Windowed); }                 // maximised: has a caption, so not "fullscreen"
    { Facts f = Base(); f.hasCaption = false; f.client = f.monitor; CHECK(Classify(f) == Kind::BorderlessFullscreen); }
    { Facts f = Base(); f.hasCaption = false; f.client = {0, 0, 2559, 1440}; CHECK(Classify(f) == Kind::BorderlessFullscreen); }   // 1 px tolerance
    { Facts f = Base(); f.hasCaption = false; f.client = {0, 0, 2550, 1440}; CHECK(Classify(f) == Kind::Windowed); }              // no caption but not filling it
    { Facts f = Base(); f.hasCaption = false; f.client = f.monitor; f.displayModeChanged = true; CHECK(Classify(f) == Kind::ExclusiveSuspected); }
    { Facts f = Base(); f.hasCaption = false; f.client = f.monitor; f.notificationState = 3; CHECK(Classify(f) == Kind::ExclusiveSuspected); }
    { Facts f = Base(); f.hasCaption = false; f.client = f.monitor; f.notificationState = 5; CHECK(Classify(f) == Kind::BorderlessFullscreen); }   // "accepts notifications": not D3D fullscreen
    { Facts f = Base(); f.notificationState = 3; CHECK(Classify(f) == Kind::Windowed); }                          // a windowed game is never "exclusive" whatever Windows says
    { Facts f = Base(); f.iconic = true; CHECK(Classify(f) == Kind::Minimized); }
    { Facts f = Base(); f.visible = false; CHECK(Classify(f) == Kind::Hidden); }
    { Facts f = Base(); f.cloaked = true; CHECK(Classify(f) == Kind::Hidden); }
    { Facts f = Base(); f.iconic = true; f.hasCaption = false; f.client = f.monitor; CHECK(Classify(f) == Kind::Minimized); }  // minimised wins

    CHECK(!Advice(Kind::ExclusiveSuspected).empty() && Advice(Kind::ExclusiveSuspected).find("borderless") != std::string::npos);
    CHECK(Advice(Kind::Windowed).empty() && Advice(Kind::BorderlessFullscreen).empty() && Advice(Kind::Minimized).empty());
    CHECK(std::string(Name(Kind::BorderlessFullscreen)) == "borderless fullscreen");

    // the raise decision
    const auto t = std::chrono::steady_clock::time_point{} + 100s;
    CHECK(ShouldRaise(true, false, t, t - 1s));
    CHECK(!ShouldRaise(true, true, t, t - 1s));            // already above
    CHECK(!ShouldRaise(false, false, t, t - 1s));          // overlay not shown: nothing to raise
    CHECK(!ShouldRaise(true, false, t, t - 100ms));        // rate limit
    CHECK(ShouldRaise(true, false, t, t - 250ms));

    std::printf(g_fail == 0 ? "ALL OK (%d checks)\n" : "%d FAILURES\n", g_fail == 0 ? g_checks : g_fail);
    return g_fail == 0 ? 0 : 1;
}
