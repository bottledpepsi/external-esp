// UiNav.h - keyboard navigation for the immediate-mode config window. Portable (no Windows or graphics headers).
//
// Every interactive control calls Stop() while it is laid out; the control whose index equals the current focus is "focused". Up/Down/Tab move
// the focus between stops, Left/Right/Enter/Space/Delete become one pending action that the focused stop takes. The window shows a ring
// around the focused stop only after a key was used (the mouse hides it again), and scrolls so the focused stop stays in view.
//
//   Up / Down, Tab / Shift+Tab   previous / next control (Tab wraps; Up/Down stop at the ends)
//   Home / End, PgUp / PgDn      first / last control, 5 controls up / down
//   Left / Right                 a cycle row goes back / forward, a toggle goes off / on, a slider steps down / up
//   Enter / Space                press the button, flip the toggle, cycle forward, start typing in a text field
//   Delete                       a slider (or any tunable) returns to its default
//   Ctrl+Tab, Ctrl+Shift+Tab     next / previous tab (Ctrl+PgDn / Ctrl+PgUp too)
//   Esc                          close the window (when no text field or key rebinding is waiting for input)
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

class UiNav
{
public:
    enum class Act { None, Prev, Next, Activate, Reset };
    static constexpr int kMaxTabs = 16;

    // Windows virtual-key codes, spelled out so this header does not need windows.h.
    enum Vk : int
    {
        kTab = 0x09, kReturn = 0x0D, kEscape = 0x1B, kSpace = 0x20, kPageUp = 0x21, kPageDown = 0x22, kEnd = 0x23, kHome = 0x24,
        kLeft = 0x25, kUp = 0x26, kRight = 0x27, kDown = 0x28, kDelete = 0x2E
    };

    // Once per frame, before the layout. `stops` = how many stops the current tab had last frame (the layout has not run yet).
    // While `capturing` (a text field or a key rebind is waiting) every key is left alone.
    void BeginFrame(const std::vector<int>& keys, bool shift, bool ctrl, bool capturing, int stops)
    {
        idx_ = 0;
        haveGeo_ = false;
        moved_ = false;
        act_ = Act::None;
        tabDelta_ = 0;
        closeRequested_ = false;
        int& f = focus_[tab_];
        if (stops > 0) f = std::clamp(f, 0, stops - 1);
        if (capturing) return;
        for (int vk : keys)
        {
            const bool isMove = vk == kUp || vk == kDown || vk == kTab || vk == kHome || vk == kEnd || vk == kPageUp || vk == kPageDown;
            const bool isAct = vk == kLeft || vk == kRight || vk == kReturn || vk == kSpace || vk == kDelete;
            if (vk == kEscape)
            {
                closeRequested_ = true;
                continue;
            }
            if (ctrl && (vk == kTab || vk == kPageDown || vk == kPageUp))
            {
                tabDelta_ += (vk == kPageUp || (vk == kTab && shift)) ? -1 : +1;
                continue;
            }
            if (!isMove && !isAct) continue;
            if (!kbActive_)
            {
                kbActive_ = true; // the first key only shows the ring - it must not act on a control the user cannot see
                if (isAct || stops == 0) continue;
            }
            if (isAct)
            {
                act_ = vk == kLeft ? Act::Prev : vk == kRight ? Act::Next : vk == kDelete ? Act::Reset : Act::Activate;
                continue;
            }
            if (stops <= 0) continue;
            const int before = f;
            switch (vk)
            {
            case kUp: f = std::max(0, f - 1); break;
            case kDown: f = std::min(stops - 1, f + 1); break;
            case kTab: f = (f + (shift ? stops - 1 : 1)) % stops; break;
            case kHome: f = 0; break;
            case kEnd: f = stops - 1; break;
            case kPageUp: f = std::max(0, f - 5); break;
            case kPageDown: f = std::min(stops - 1, f + 5); break;
            }
            if (f != before) moved_ = true;
        }
    }

    // The tab change the keys asked for this frame (taken once; the caller wraps it).
    int TakeTabDelta()
    {
        const int d = tabDelta_;
        tabDelta_ = 0;
        return d;
    }
    bool CloseRequested() const { return closeRequested_; }

    // Called every frame with the tab being shown. Only a CHANGE of tab brings that tab's focused control into view: the config window calls this on
    // every frame, and flagging the focus as "moved" each time made ScrollFor() put the scroll back on the focused control after every wheel notch
    // (with the focus on the first control: back to the top), so a tab taller than the window could not be scrolled down.
    void SetTab(int t)
    {
        const int n = std::clamp(t, 0, kMaxTabs - 1);
        if (n != tab_) moved_ = true; // bring the new tab's focus into view
        tab_ = n;
    }
    int Tab() const { return tab_; }

    // Called by every interactive control while it is laid out, top to bottom. (y, h) are in content coordinates (the same on every frame whatever
    // the scroll). True when this control is the focused one.
    bool Stop(float y, float h)
    {
        const bool focused = idx_ == focus_[tab_];
        if (focused)
        {
            fy_ = y;
            fh_ = h;
            haveGeo_ = true;
        }
        ++idx_;
        return focused;
    }
    // The action for the focused stop (call only after Stop() returned true); it is handed out once.
    Act Take()
    {
        const Act a = act_;
        act_ = Act::None;
        return a;
    }
    int StopCount() const { return idx_; }
    int Focus() const { return focus_[tab_]; }
    void SetFocus(int i) { focus_[tab_] = std::max(0, i); }
    bool RingVisible() const { return kbActive_; }

    // The mouse took over: hide the ring.
    void OnMouse() { kbActive_ = false; }
    void Reset()
    {
        focus_.fill(0);
        kbActive_ = false;
    }

    // After the layout: the scroll offset that keeps the focused stop inside a view of height `viewH` (only when the focus moved this frame, so
    // wheel scrolling is not fought). `scroll` and the result are offsets into the content.
    float ScrollFor(float scroll, float viewH, float margin = 6.f) const
    {
        if (!moved_ || !haveGeo_ || viewH <= 0) return scroll;
        if (focus_[tab_] == 0) return 0.f; // the first control: show the section title above it as well
        if (fy_ - margin < scroll) return std::max(0.f, fy_ - margin);
        if (fy_ + fh_ + margin > scroll + viewH) return fy_ + fh_ + margin - viewH;
        return scroll;
    }

private:
    std::array<int, kMaxTabs> focus_{};
    int tab_ = 0, idx_ = 0, tabDelta_ = 0;
    bool kbActive_ = false, moved_ = false, closeRequested_ = false, haveGeo_ = false;
    Act act_ = Act::None;
    float fy_ = 0, fh_ = 0;
};
