// WindowState.h - what state is the game window in, and does the overlay have to be raised above it. Portable (no Windows types): WindowTracker gathers the
// facts, tests/window_state_test.cpp feeds this file hand-made ones.
#pragma once
#include <chrono>
#include <cstdlib>
#include <string>

namespace winstate
{
    struct Rect
    {
        int l = 0, t = 0, r = 0, b = 0;
        int W() const { return r - l; }
        int H() const { return b - t; }
    };

    struct Facts
    {
        bool iconic = false, visible = true, cloaked = false; // minimised / not shown / on another virtual desktop (DWM cloaked)
        bool hasCaption = true;                               // WS_CAPTION: a title bar and border
        Rect client;                                          // the game's client area in screen pixels
        Rect monitor;                                         // the monitor it is mostly on
        bool displayModeChanged = false;                      // that monitor's current mode differs from the desktop's registry mode (a mode switch)
        int notificationState = -1;                           // SHQueryUserNotificationState; 3 = a Direct3D full-screen application runs; -1 unknown
    };

    enum class Kind { Windowed, BorderlessFullscreen, ExclusiveSuspected, Minimized, Hidden };

    inline bool CoversMonitor(const Rect& c, const Rect& m, int tol = 1)
    {
        return std::abs(c.l - m.l) <= tol && std::abs(c.t - m.t) <= tol && std::abs(c.r - m.r) <= tol && std::abs(c.b - m.b) <= tol;
    }

    // A window that fills a monitor without a title bar is "fullscreen". Whether it is exclusive (nothing may be drawn over it) cannot be known for sure: a
    // display mode switch, or Windows reporting a Direct3D full-screen application, are the two signals that exist. Minecraft is OpenGL, where GLFW's
    // "fullscreen" is normally a borderless window the compositor can draw over, so ExclusiveSuspected should be rare - and is only ever a warning.
    inline Kind Classify(const Facts& f)
    {
        if (f.iconic) return Kind::Minimized;
        if (!f.visible || f.cloaked) return Kind::Hidden;
        if (!f.hasCaption && CoversMonitor(f.client, f.monitor))
            return (f.displayModeChanged || f.notificationState == 3) ? Kind::ExclusiveSuspected : Kind::BorderlessFullscreen;
        return Kind::Windowed;
    }

    inline const char* Name(Kind k)
    {
        switch (k)
        {
        case Kind::Windowed: return "windowed";
        case Kind::BorderlessFullscreen: return "borderless fullscreen";
        case Kind::ExclusiveSuspected: return "exclusive fullscreen (suspected)";
        case Kind::Minimized: return "minimised";
        default: return "hidden";
        }
    }

    // Empty when there is nothing to tell the user.
    inline std::string Advice(Kind k)
    {
        if (k != Kind::ExclusiveSuspected) return {};
        return "The game looks like it runs in exclusive fullscreen: Windows does not draw other windows over it, so the overlay can be invisible. "
               "Switch the game to windowed or borderless (Minecraft: Options > Video Settings > Fullscreen off, or F11).";
    }

    // The overlay is supposed to sit above the game. A topmost game window (GLFW fullscreen) that was re-activated lands above our topmost overlay, which then
    // looks "blank". Re-asserting TOPMOST puts the overlay back on top of the topmost band. At most every 250 ms.
    inline bool ShouldRaise(bool overlayShown, bool aboveGame, std::chrono::steady_clock::time_point now, std::chrono::steady_clock::time_point lastRaise)
    {
        return overlayShown && !aboveGame && now - lastRaise >= std::chrono::milliseconds(250);
    }
} // namespace winstate
