// WindowTracker.h - gathers WindowState facts about the game window from Win32 and keeps the overlay above it. Windows-only.
#pragma once
#include "WindowState.h"
#include <windows.h>

namespace wintrack
{
    winstate::Facts Gather(HWND game);

    // Is `overlay` above `game` in the z-order? (Walks upwards from the game window through GW_HWNDPREV.)
    bool OverlayAbove(HWND overlay, HWND game);

    // Re-asserts TOPMOST without activating or moving the overlay.
    void Raise(HWND overlay);
} // namespace wintrack
