// ui_nav_test - keyboard navigation of the config window: focus movement, actions, capture, tab keys, scroll-into-view.
#include "UiNav.h"
#include <cstdio>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
using K = UiNav;

// lays out `n` stops of height 24 (like rows) and returns the index of the focused one (-1 none) and its action
struct Frame { int focused = -1; UiNav::Act act = UiNav::Act::None; };
static Frame Layout(UiNav& nav, int n)
{
    Frame f;
    for (int i = 0; i < n; ++i)
        if (nav.Stop(i * 24.f, 22.f)) { f.focused = i; f.act = nav.Take(); }
    return f;
}
static Frame Press(UiNav& nav, std::vector<int> keys, int n, bool shift = false, bool ctrl = false, bool capturing = false)
{
    nav.BeginFrame(keys, shift, ctrl, capturing, n);
    return Layout(nav, n);
}

int main()
{
    // ---- the first key only shows the ring; it must not act on an invisible control ----
    {
        UiNav nav;
        Press(nav, {}, 5);
        CHECK(!nav.RingVisible());
        Frame f = Press(nav, {K::kReturn}, 5);
        CHECK(f.focused == 0 && f.act == UiNav::Act::None && nav.RingVisible());
        f = Press(nav, {K::kReturn}, 5);
        CHECK(f.act == UiNav::Act::Activate);
        nav.OnMouse();
        CHECK(!nav.RingVisible());
        f = Press(nav, {K::kRight}, 5); // hidden again: shows, does not act
        CHECK(f.act == UiNav::Act::None && nav.RingVisible());
    }
    // ---- movement ----
    {
        UiNav nav;
        Press(nav, {K::kDown}, 6);                   // reveals + moves
        CHECK(nav.Focus() == 1);
        CHECK(Press(nav, {K::kDown, K::kDown}, 6).focused == 3);
        CHECK(Press(nav, {K::kUp}, 6).focused == 2);
        CHECK(Press(nav, {K::kEnd}, 6).focused == 5);
        CHECK(Press(nav, {K::kDown}, 6).focused == 5); // stops at the end
        CHECK(Press(nav, {K::kHome}, 6).focused == 0);
        CHECK(Press(nav, {K::kUp}, 6).focused == 0);   // and at the start
        CHECK(Press(nav, {K::kPageDown}, 8).focused == 5);
        CHECK(Press(nav, {K::kPageUp}, 8).focused == 0);
        // Tab wraps both ways
        CHECK(Press(nav, {K::kTab}, 3).focused == 1);
        CHECK(Press(nav, {K::kTab, K::kTab}, 3).focused == 0);
        CHECK(Press(nav, {K::kTab}, 3, true).focused == 2);
        CHECK(Press(nav, {K::kTab}, 3, true).focused == 1);
    }
    // ---- actions reach the focused stop only, once ----
    {
        UiNav nav;
        Press(nav, {K::kDown}, 4);
        Frame f = Press(nav, {K::kLeft}, 4);
        CHECK(f.focused == 1 && f.act == UiNav::Act::Prev);
        CHECK(Press(nav, {K::kRight}, 4).act == UiNav::Act::Next);
        CHECK(Press(nav, {K::kSpace}, 4).act == UiNav::Act::Activate);
        CHECK(Press(nav, {K::kDelete}, 4).act == UiNav::Act::Reset);
        CHECK(Press(nav, {}, 4).act == UiNav::Act::None);          // nothing pending next frame
        // a frame in which nothing is focused (no stops) must not leave an action for the next one
        nav.BeginFrame({K::kRight}, false, false, false, 4);
        CHECK(Layout(nav, 0).act == UiNav::Act::None);
        CHECK(Press(nav, {}, 4).act == UiNav::Act::None);
    }
    // ---- capturing (typing / rebinding) leaves every key alone ----
    {
        UiNav nav;
        Press(nav, {K::kDown}, 5);
        const int at = nav.Focus();
        Frame f = Press(nav, {K::kDown, K::kReturn, K::kEscape, K::kTab}, 5, false, false, true);
        CHECK(nav.Focus() == at && f.act == UiNav::Act::None && !nav.CloseRequested());
    }
    // ---- Esc closes, tab keys ----
    {
        UiNav nav;
        nav.BeginFrame({K::kEscape}, false, false, false, 3);
        CHECK(nav.CloseRequested());
        nav.BeginFrame({}, false, false, false, 3);
        CHECK(!nav.CloseRequested());
        nav.BeginFrame({K::kTab}, false, true, false, 3);                    // Ctrl+Tab
        CHECK(nav.TakeTabDelta() == 1 && nav.TakeTabDelta() == 0);
        nav.BeginFrame({K::kTab}, true, true, false, 3);                     // Ctrl+Shift+Tab
        CHECK(nav.TakeTabDelta() == -1);
        nav.BeginFrame({K::kPageDown}, false, true, false, 3);
        CHECK(nav.TakeTabDelta() == 1);
        nav.BeginFrame({K::kPageUp}, false, true, false, 3);
        CHECK(nav.TakeTabDelta() == -1);
        nav.BeginFrame({K::kPageUp}, false, false, false, 3);                // without Ctrl it moves the focus, not the tab
        CHECK(nav.TakeTabDelta() == 0);
    }
    // ---- focus is remembered per tab, and clamped when a tab shrinks ----
    {
        UiNav nav;
        nav.SetTab(0);
        Press(nav, {K::kEnd}, 7);
        CHECK(nav.Focus() == 6);
        nav.SetTab(3);
        CHECK(nav.Focus() == 0);
        Press(nav, {K::kDown}, 4);
        CHECK(nav.Focus() == 1);
        nav.SetTab(0);
        CHECK(nav.Focus() == 6);
        nav.BeginFrame({}, false, false, false, 3); // the tab lost rows (a profile was deleted)
        CHECK(nav.Focus() == 2);
    }
    // ---- scroll keeps the focused stop in view ----
    {
        UiNav nav;
        Press(nav, {K::kDown}, 30);
        Layout(nav, 0); // (geometry is recorded by the Press above)
        UiNav n2;
        n2.BeginFrame({K::kEnd}, false, false, false, 30);
        Layout(n2, 30);
        const float viewH = 390.f;
        float s = n2.ScrollFor(0.f, viewH);                       // last stop at y=696..718: scroll so it sits at the bottom
        CHECK(s > 0 && 29 * 24.f + 22.f + 6.f - s <= viewH + 0.01f && 29 * 24.f - s >= 0);
        UiNav n3;                                                  // moving back up
        n3.BeginFrame({K::kEnd, K::kHome}, false, false, false, 30);
        Layout(n3, 30);
        CHECK(n3.ScrollFor(300.f, viewH) == 0.f);                  // first stop: all the way up (the section title shows)
        UiNav n4;                                                  // already visible: no change
        n4.BeginFrame({K::kDown, K::kDown}, false, false, false, 30);
        Layout(n4, 30);
        CHECK(n4.ScrollFor(0.f, viewH) == 0.f);
        UiNav n5;                                                  // wheel scrolling is not fought when the focus did not move
        n5.BeginFrame({}, false, false, false, 30);
        Layout(n5, 30);
        CHECK(n5.ScrollFor(200.f, viewH) == 200.f);
        UiNav n6;                                                  // a focus above the view scrolls up to it
        n6.BeginFrame({K::kDown, K::kPageDown}, false, false, false, 30);
        Layout(n6, 30);
        const float up = n6.ScrollFor(500.f, viewH);
        CHECK(up < 500.f && up <= n6.Focus() * 24.f);
    }

    // ---- the window's real frame sequence: BeginFrame, SetTab(the same tab) on EVERY frame, layout, ScrollFor. The wheel must be able to scroll down. ----
    {
        const float viewH = 390.f;
        const int stops = 30; // a tab taller than the window
        UiNav nav;
        float scroll = 0.f;
        auto frame = [&](std::vector<int> keys, int tab)
        {
            nav.BeginFrame(keys, false, false, false, stops);
            nav.SetTab(tab);
            Layout(nav, stops);
            scroll = nav.ScrollFor(scroll, viewH);
        };
        frame({}, 0);
        CHECK(scroll == 0.f);
        // the user scrolls with the wheel (the window changes `scroll` between frames, then calls ScrollFor after the layout)
        for (int i = 0; i < 10; ++i)
        {
            scroll += 28.f; // a wheel notch down
            const float before = scroll;
            frame({}, 0);
            CHECK(scroll == before); // not pulled back to the focused control (the first one: that would be the top)
        }
        CHECK(scroll == 280.f);
        // the focus ring on the first control, still not fought
        frame({K::kDown}, 0); // first key only shows the ring
        scroll = 200.f;
        frame({}, 0);
        CHECK(scroll == 200.f);
        // keyboard movement still scrolls to the focused control: End -> the last one comes into view
        frame({K::kEnd}, 0);
        CHECK(scroll > 200.f && 29 * 24.f + 22.f + 6.f - scroll <= viewH + 0.01f);
        const float atEnd = scroll;
        frame({}, 0); // and the next frame leaves it where it is
        CHECK(scroll == atEnd);
        // switching tab brings the new tab's focused control into view once (a tab whose focus is far down opens scrolled to it) ...
        UiNav n2;
        n2.BeginFrame({K::kDown, K::kEnd}, false, false, false, stops);
        n2.SetTab(0);
        Layout(n2, stops);
        float s2 = n2.ScrollFor(0.f, viewH);
        CHECK(s2 > 0);
        n2.BeginFrame({}, false, false, false, stops);
        n2.SetTab(2); // another tab
        Layout(n2, stops);
        CHECK(n2.ScrollFor(300.f, viewH) == 0.f); // its focus is the first control: shown from the top
        n2.BeginFrame({}, false, false, false, stops);
        n2.SetTab(2); // the same tab again: the user may scroll
        Layout(n2, stops);
        CHECK(n2.ScrollFor(300.f, viewH) == 300.f);
        n2.BeginFrame({}, false, false, false, stops);
        n2.SetTab(99); // clamped to the last tab, which is a change
        Layout(n2, stops);
        CHECK(n2.Tab() == UiNav::kMaxTabs - 1 && n2.ScrollFor(300.f, viewH) == 0.f);
    }

    if (fails) std::printf("%d failure(s)\n", fails); else std::puts("ui_nav_test: all passed");
    return fails ? 1 : 0;
}
