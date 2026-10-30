// wintrack_probe.cpp - Windows-only probe of src/WindowTracker.cpp: creates stand-in "game" windows of each kind and checks what the tracker says.
// Runs on Windows and under Wine (+ Xvfb). Under Wine the window manager is not Windows': a pass shows the Win32 calls are used sanely, not that z-order,
// DWM cloaking or display-mode switches behave as on Windows (those two facts are only ever "false"/"unknown" there).
//   x86_64-w64-mingw32-g++ -std=c++20 tests/windows/wintrack_probe.cpp src/WindowTracker.cpp -o wintrack_probe.exe -luser32 -lgdi32 -static-libgcc -static-libstdc++
#include "../../src/WindowTracker.h"
#include <cstdio>

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

static HWND Make(DWORD style, DWORD ex, int x, int y, int w, int h, const wchar_t* title)
{
    return CreateWindowExW(ex, L"probe", title, style, x, y, w, h, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}

static void Pump() { MSG m; for (int i = 0; i < 20; ++i) { while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&m); Sleep(10); } }

int main()
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = Proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"probe";
    RegisterClassW(&wc);
    const int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    std::printf("screen %dx%d\n", sw, sh);

    // 1. an ordinary window
    HWND win = Make(WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 100, 100, 800, 600, L"Minecraft* 1.21.1");
    Pump();
    winstate::Facts f = wintrack::Gather(win);
    std::printf("windowed: client %dx%d caption=%d visible=%d -> %s\n", f.client.W(), f.client.H(), f.hasCaption, f.visible, winstate::Name(winstate::Classify(f)));
    CHECK(f.visible && f.hasCaption && !f.iconic && f.client.W() > 700 && f.client.W() <= 800);
    CHECK(winstate::Classify(f) == winstate::Kind::Windowed);

    // 2. minimised
    ShowWindow(win, SW_MINIMIZE);
    Pump();
    f = wintrack::Gather(win);
    std::printf("minimised: iconic=%d -> %s\n", f.iconic, winstate::Name(winstate::Classify(f)));
    CHECK(winstate::Classify(f) == winstate::Kind::Minimized);
    DestroyWindow(win);

    // 3. borderless fullscreen: a popup that covers the monitor, no caption
    HWND fs = Make(WS_POPUP | WS_VISIBLE, WS_EX_TOPMOST, 0, 0, sw, sh, L"Minecraft* 1.21.1");
    Pump();
    f = wintrack::Gather(fs);
    std::printf("fullscreen: client %dx%d at %d,%d monitor %dx%d caption=%d modeSwitched=%d qstate=%d -> %s\n", f.client.W(), f.client.H(), f.client.l, f.client.t, f.monitor.W(),
                f.monitor.H(), f.hasCaption, f.displayModeChanged, f.notificationState, winstate::Name(winstate::Classify(f)));
    CHECK(!f.hasCaption && f.client.W() == sw && f.client.H() == sh);
    const winstate::Kind k = winstate::Classify(f);
    CHECK(k == winstate::Kind::BorderlessFullscreen || k == winstate::Kind::ExclusiveSuspected);   // the second only if the platform reports a mode switch / D3D fullscreen

    // 4. the z-order watchdog: an overlay window above a topmost game stays above; after the game is raised over it, OverlayAbove says no and Raise fixes it
    HWND ov = Make(WS_POPUP | WS_VISIBLE, WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, 100, 100, 300, 200, L"overlay");
    Pump();
    SetWindowPos(fs, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);          // the game window is activated / re-asserted and lands above the overlay
    Pump();
    const bool above = wintrack::OverlayAbove(ov, fs);
    std::printf("overlay above game after the game was raised: %d (expected 0)\n", above);
    wintrack::Raise(ov);
    Pump();
    const bool above2 = wintrack::OverlayAbove(ov, fs);
    std::printf("overlay above game after Raise(): %d (expected 1)\n", above2);
    CHECK(!above);
    CHECK(above2);
    CHECK(wintrack::OverlayAbove(nullptr, fs) && wintrack::OverlayAbove(ov, nullptr));   // nothing to compare: do not raise

    // 5. a destroyed window is "hidden", never a crash
    DestroyWindow(fs);
    f = wintrack::Gather(fs);
    CHECK(winstate::Classify(f) == winstate::Kind::Hidden);
    DestroyWindow(ov);
    std::printf(g_fail == 0 ? "ALL OK (%d checks)\n" : "%d FAILURES\n", g_fail == 0 ? g_checks : g_fail);
    return g_fail == 0 ? 0 : 1;
}
