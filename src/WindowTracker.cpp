#include "WindowTracker.h"

namespace wintrack
{
    namespace
    {
        winstate::Rect R(const RECT& r) { return {r.left, r.top, r.right, r.bottom}; }

        // The desktop's own mode for the monitor (registry) vs the mode it is in now: different = something switched the display mode (exclusive fullscreen).
        bool ModeSwitched(const MONITORINFOEXW& mi)
        {
            DEVMODEW cur{}, reg{};
            cur.dmSize = reg.dmSize = sizeof(DEVMODEW);
            if (!EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &cur) || !EnumDisplaySettingsW(mi.szDevice, ENUM_REGISTRY_SETTINGS, &reg)) return false;
            return cur.dmPelsWidth != reg.dmPelsWidth || cur.dmPelsHeight != reg.dmPelsHeight;
        }

        // SHQueryUserNotificationState (shell32): 3 = QUNS_RUNNING_D3D_FULL_SCREEN. Loaded dynamically; -1 when unavailable.
        int NotificationState()
        {
            using Fn = HRESULT(WINAPI*)(int*);
            HMODULE sh = LoadLibraryW(L"shell32.dll");
            auto f = sh ? reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(sh, "SHQueryUserNotificationState"))) : nullptr;
            int st = -1;
            return f && SUCCEEDED(f(&st)) ? st : -1;
        }

        bool Cloaked(HWND h)
        {
            using Fn = HRESULT(WINAPI*)(HWND, DWORD, PVOID, DWORD);
            HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
            auto f = dwm ? reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(dwm, "DwmGetWindowAttribute"))) : nullptr;
            DWORD c = 0;
            return f && SUCCEEDED(f(h, 14 /*DWMWA_CLOAKED*/, &c, sizeof c)) && c != 0;
        }
    } // namespace

    winstate::Facts Gather(HWND game)
    {
        winstate::Facts f;
        if (!game || !IsWindow(game)) { f.visible = false; return f; }
        f.iconic = IsIconic(game) != FALSE;
        f.visible = IsWindowVisible(game) != FALSE;
        f.cloaked = Cloaked(game);
        f.hasCaption = (GetWindowLongW(game, GWL_STYLE) & WS_CAPTION) == WS_CAPTION;
        RECT rc{};
        GetClientRect(game, &rc);
        POINT p{0, 0};
        ClientToScreen(game, &p);
        f.client = {p.x, p.y, p.x + rc.right, p.y + rc.bottom};
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof mi;
        if (GetMonitorInfoW(MonitorFromWindow(game, MONITOR_DEFAULTTONEAREST), &mi))
        {
            f.monitor = R(mi.rcMonitor);
            f.displayModeChanged = ModeSwitched(mi);
        }
        f.notificationState = NotificationState();
        return f;
    }

    bool OverlayAbove(HWND overlay, HWND game)
    {
        if (!overlay || !game) return true;
        int guard = 0;
        for (HWND h = GetWindow(game, GW_HWNDPREV); h && guard < 4096; h = GetWindow(h, GW_HWNDPREV), ++guard)
            if (h == overlay) return true;
        return false;
    }

    void Raise(HWND overlay)
    {
        SetWindowPos(overlay, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
} // namespace wintrack
