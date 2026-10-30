#include "EspOverlay.h"
#include "Clipboard.h"
#include "DiagnosticsApp.h"
#include "Version.h"
#include "FriendList.h"
#include "Logger.h"
#include "Presets.h"
#include "ProfileManager.h"
#include "SkinCache.h"
#include <dwmapi.h>
#include <algorithm>
#include <cstdio>
#include <timeapi.h>

using namespace std::chrono_literals;

#ifndef WS_EX_NOREDIRECTIONBITMAP
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#endif

namespace
{
    constexpr wchar_t kClass[] = L"ExternalEspOverlay";
    constexpr auto kExitHold = 3s;
} // namespace

EspOverlay::EspOverlay(const AppOptions& opt)
    : opt_(opt),
      ui_(settings_),
      udp_(opt.port),
      injector_(InjectorConfig{opt.jarPath, opt.javaPath, opt.port, opt.agentHz, opt.verbose, opt.token, opt.agentExtraOptions()}),
      sup_(opt_, udp_, injector_)
{
}

EspOverlay::~EspOverlay()
{
    if (hwnd_&& IsWindow(hwnd_)) DestroyWindow(hwnd_);
}

// ------------------------------------------------------------------------------------------------ window

bool EspOverlay::CreateOverlayWindow()
{
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = &EspOverlay::WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512) /* IDC_ARROW */);
    wc.lpszClassName = kClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        LOG_ERROR("RegisterClassEx failed (%lu)", static_cast<unsigned long>(GetLastError()));
        return false;
    }
    // WS_EX_LAYERED     : per-pixel alpha via UpdateLayeredWindow
    // WS_EX_TRANSPARENT : mouse clicks fall through to the game (removed while the config window is open)
    // WS_EX_TOPMOST     : stays above the game
    // WS_EX_TOOLWINDOW  : no taskbar button / Alt-Tab entry
    // WS_EX_NOACTIVATE  : never steals keyboard focus from the game (removed while the config window is open)
    // WS_EX_NOREDIRECTIONBITMAP : (GPU renderer only) no GDI redirection surface - the window's pixels come from the
    //                             DirectComposition swap chain instead of UpdateLayeredWindow
    DWORD exStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    if (gfx_.IsGpu()) exStyle |= WS_EX_NOREDIRECTIONBITMAP;
    hwnd_ = CreateWindowExW(exStyle, kClass, L"external-esp", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, wc.hInstance,
                            this);
    if (!hwnd_)
    {
        LOG_ERROR("CreateWindowEx failed (%lu)", static_cast<unsigned long>(GetLastError()));
        return false;
    }
    clickThrough_ = true;
    return true;
}

LRESULT CALLBACK EspOverlay::WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    EspOverlay * self = nullptr;
    if (m == WM_NCCREATE)
    {
        self = static_cast<EspOverlay*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<EspOverlay*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    }
    return self ? self->OnMessage(h, m, w, l) : DefWindowProcW(h, m, w, l);
}

LRESULT EspOverlay::OnMessage(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEACTIVATE: return configOpen_ ? MA_ACTIVATE : MA_NOACTIVATE;
    case WM_SETCURSOR:
        if (configOpen_)
        {
            SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE:
        pend_.mx = static_cast<float>(static_cast<short>(LOWORD(l)));
        pend_.my = static_cast<float>(static_cast<short>(HIWORD(l)));
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(h); // keep receiving moves while dragging the panel
        pend_.down = true;
        pend_.pressed = true;
        return 0;
    case WM_LBUTTONUP:
        ReleaseCapture();
        pend_.down = false;
        pend_.released = true;
        return 0;
    case WM_RBUTTONDOWN: pend_.rpressed = true;
        return 0;
    case WM_MOUSEWHEEL: pend_.wheel += static_cast<float>(GET_WHEEL_DELTA_WPARAM(w)) / static_cast<float>(WHEEL_DELTA);
        return 0;
    case WM_CHAR:
        if (configOpen_&& w>= 32 && w < 127) pend_.chars.push_back(static_cast<char>(w));
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (configOpen_) pend_.keys.push_back(static_cast<int>(w));
        return 0;
    case WM_CLOSE: DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        if (!suppressQuit_)
        {
            quit_ = true;
            PostQuitMessage(0);
        }
        return 0;
    default: break;
    }
    return DefWindowProcW(h, m, w, l);
}

void EspOverlay::SetClickThrough(bool on)
{
    if (!hwnd_ || on == clickThrough_) return;
    LONG_PTR ex = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    if (on) ex |= (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
    else ex &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex);
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    clickThrough_ = on;
}

// SetForegroundWindow is restricted for background processes; briefly attaching to the foreground
// thread's input queue is the standard workaround.
void EspOverlay::ForceForeground(HWND h)
{
    HWND fg = GetForegroundWindow();
    const DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0, me = GetCurrentThreadId();
    if (fgThread && fgThread != me) AttachThreadInput(me, fgThread, TRUE);
    SetForegroundWindow(h);
    SetFocus(h);
    if (fgThread && fgThread != me) AttachThreadInput(me, fgThread, FALSE);
}

void EspOverlay::SetShown(bool show)
{
    if (show == shown_ || !hwnd_) return;
    if (show)
    {
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    else
    {
        ShowWindow(hwnd_, SW_HIDE);
    }
    shown_ = show;
}

void EspOverlay::OpenConfig()
{
    configOpen_ = true;
    configOpenedAt_ = Clock::now();
    pend_ = UiInput{};
    ui_.ResetFocus();
    ProfileManager::Refresh();
    SetShown(true);
    SetClickThrough(false); // the window now receives the mouse
    ForceForeground(hwnd_); // ...and keyboard focus (text fields, rebinding)
}

void EspOverlay::CloseConfig()
{
    configOpen_ = false;
    ui_.ResetFocus();
    SetClickThrough(true);
    pend_ = UiInput{};
    HWND mc = sup_.GameWindow();
    if (mc && IsWindow(mc)) SetForegroundWindow(mc); // hand focus back to the game
}

// ------------------------------------------------------------------------------------------------ input

bool EspOverlay::KeyEdge(int vk)
{
    const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool& prev = prevKeys_[vk];
    const bool edge = down && !prev;
    prev = down;
    return edge;
}

// --replay only: F6 pause/resume, F7 step one datagram (while paused), F8 half speed, F9 double speed.
std::string EspOverlay::CopyDiagnostics()
{
    diag::AppContext ctx;
    ctx.version = kEspVersion;
    ctx.agentLog = opt_.jarPath.parent_path() / "esp_agent.log";
    ctx.token = opt_.token;
    ctx.renderer = gfx_.IsGpu() ? "GPU (D3D11 flip swap chain + DirectComposition + Direct2D device context)" : "legacy (DIB + UpdateLayeredWindow)";
    ctx.hudStatus = lastStatus_;
    char b[512];
    std::snprintf(b, sizeof b, "port = %d\nagent hz = %d\ntoken check = %s\nmode = %s\nmotion smoothing = %s\nvsync = %s\nfps cap index = %d\nverbose = %s\n", static_cast<int>(opt_.port),
                  opt_.agentHz, opt_.requireAuth ? "on" : "OFF", replayMode_ ? "replay" : (opt_.demo ? "demo" : "agent"), settings_.smoothing ? "on" : "off",
                  settings_.vsync ? "on" : "off", settings_.fpsCap, opt_.verbose ? "on" : "off");
    ctx.settings = b;
    {
        const auto link = Src().Link();
        char sb[400], seen[32], bad[32];
        if (Src().AcceptedVersion() != 0) std::snprintf(seen, sizeof seen, "v%d", Src().AcceptedVersion()); else std::snprintf(seen, sizeof seen, "none");
        if (Src().UnsupportedVersionSeen() != 0) std::snprintf(bad, sizeof bad, "v%d", Src().UnsupportedVersionSeen()); else std::snprintf(bad, sizeof bad, "none");
        std::snprintf(sb, sizeof sb, "published %llu, rejected %llu (auth %llu), numbered %llu, lost %llu, reordered %llu, duplicate %llu\nagent protocol seen: %s (unsupported seen: %s)",
                      static_cast<unsigned long long>(Src().PacketCount()), static_cast<unsigned long long>(Src().RejectedCount()), static_cast<unsigned long long>(Src().AuthFailures()),
                      static_cast<unsigned long long>(link.numbered), static_cast<unsigned long long>(link.lost), static_cast<unsigned long long>(link.reordered),
                      static_cast<unsigned long long>(link.duplicates), seen, bad);
        ctx.streamStats = sb;
    }
    if (snap_.localValid) ctx.localName = snap_.local.name;
    for (const auto& p : snap_.players) ctx.playerNames.push_back(p.name);
    const std::string report = diag::Collect(ctx);
    if (!clipboard::SetText(report)) return "Could not open the clipboard (another program holds it?). Try again, or run: mcesp --diagnostics report.txt";
    LOG_INFO("Diagnostics report copied to the clipboard (%llu bytes).", static_cast<unsigned long long>(report.size()));
    char note[96];
    std::snprintf(note, sizeof note, "Copied %llu KB to the clipboard (redacted).", static_cast<unsigned long long>((report.size() + 1023) / 1024));
    return note;
}

void EspOverlay::HandleReplayKeys()
{
    if (KeyEdge(VK_F6)) replayDriver_.TogglePause();
    if (KeyEdge(VK_F7)) replayDriver_.RequestStep();
    if (KeyEdge(VK_F8)) replayDriver_.SetSpeed(replayDriver_.Speed() * 0.5);
    if (KeyEdge(VK_F9)) replayDriver_.SetSpeed(replayDriver_.Speed() * 2.0);
}

void EspOverlay::HandleHotkeys(bool focused)
{
    if (!focused || ui_.CapturingKeys())
    {
        // global hotkeys only act while the game/overlay has focus
        exitHolding_ = false;
        // keep edge state fresh so a key held while unfocused does not fire on return
        KeyEdge(settings_.vkToggle);
        KeyEdge(settings_.vkConfig);
        return;
    }
    if (replayMode_) HandleReplayKeys();
    if (KeyEdge(settings_.vkToggle))
    {
        visible_ = !visible_;
        LOG_INFO("ESP %s", visible_ ? "shown" : "hidden");
    }
    if (KeyEdge(settings_.vkConfig))
    {
        if (configOpen_) CloseConfig();
        else OpenConfig();
    }

    const bool exitDown = (GetAsyncKeyState(settings_.vkExit) & 0x8000) != 0;
    if (exitDown)
    {
        if (!exitHolding_)
        {
            exitHolding_ = true;
            exitHoldStart_ = Clock::now();
        }
        else if (Clock::now() - exitHoldStart_ >= kExitHold)
        {
            LOG_INFO("Exit hotkey held - shutting down.");
            quit_ = true;
        }
    }
    else
    {
        exitHolding_ = false;
    }
}

// ------------------------------------------------------------------------------------------------ geometry / pacing

bool EspOverlay::GameGeometry(RECT& r) const
{
    if (opt_.demo || opt_.screenOverlay)
    {
        r = RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
        return true;
    }
    HWND mc = sup_.GameWindow();
    if (!mc || !IsWindow(mc) || IsIconic(mc)) return false;
    RECT rc{};
    GetClientRect(mc, &rc);
    POINT p{0, 0};
    ClientToScreen(mc, &p); // client origin in screen space (excludes title bar / borders)
    r = RECT{p.x, p.y, p.x + rc.right, p.y + rc.bottom};
    return rc.right > 16 && rc.bottom > 16;
}

void EspOverlay::Pace(Clock::time_point frameStart)
{
    int cap = presets::FpsCaps[settings_.fpsCap];
    if (cap == 0 && !settings_.vsync) cap = 240; // never spin a core flat out
    if (cap <= 0) return; // unlimited + VSync: DwmFlush already paces us
    const auto target = frameStart + std::chrono::microseconds(1'000'000 / cap);
    for (;;)
    {
        const auto now = Clock::now();
        if (now >= target) break;
        if (target - now > 2ms) Sleep(1);
        else SwitchToThread();
    }
}

// ------------------------------------------------------------------------------------------------ main loop

int EspOverlay::Run()
{
    settings_.Load();
    if (!gfx_.Init(!opt_.legacyGfx))
    {
        LOG_ERROR("Direct2D/DirectWrite initialisation failed.");
        return 1;
    }
    if (!CreateOverlayWindow()) return 1;
    if (gfx_.IsGpu() && !gfx_.Attach(hwnd_))
    {
        // The window was created for the GPU renderer (no redirection bitmap); the legacy one needs a normal
        // layered window, so build it again.
        LOG_WARN("DirectComposition attach failed - falling back to the legacy layered-window renderer.");
        suppressQuit_ = true;
        DestroyWindow(hwnd_);
        suppressQuit_ = false;
        hwnd_ = nullptr;
        if (!gfx_.ForceLegacy() || !CreateOverlayWindow())
        {
            LOG_ERROR("Legacy renderer initialisation failed.");
            return 1;
        }
    }
    LOG_INFO("Renderer: %s", gfx_.IsGpu()
                                 ? "GPU (D3D11 flip swap chain + DirectComposition + Direct2D device context)"
                                 : "legacy (DIB + UpdateLayeredWindow)");
    if (!opt_.recordPath.empty())
    {
        std::string err;
        if (!recorder_.Start(opt_.recordPath, &err))
        {
            LOG_ERROR("--record: %s", err.c_str());
            return 1;
        }
        udp_.SetTap(&recorder_);
    }
    replayMode_ = !opt_.replayPath.empty();
    ui_.SetDiagnosticsHandler([this] { return CopyDiagnostics(); });
    nextLinkLog_ = Clock::now() + std::chrono::seconds(10); // no log line about the link in the first seconds: the counts mean nothing yet
    if (replayMode_)
    {
        std::string err;
        if (!replayPlayer_.Load(opt_.replayPath, &err))
        {
            LOG_ERROR("--replay: %s", err.c_str());
            std::fprintf(stderr, "--replay: %s\n", err.c_str());
            return 1;
        }
        replayPlayer_.SetSpeed(opt_.replaySpeed > 0 ? opt_.replaySpeed : 1.0);
        replayPlayer_.SetLoop(opt_.replayLoop);
        replayIngest_.SetSeqCheck(false); // a looping recording restarts its datagram numbers
        replayDriver_.Start();
    }
    else if (!opt_.demo)
    {
        udp_.Ingest().SetAuth(opt_.requireAuth, opt_.token);
        if (!opt_.requireAuth) LOG_WARN("Session token check is OFF (--no-auth): any program that can reach 127.0.0.1:%d can feed this overlay.", static_cast<int>(opt_.port));
        if (!udp_.Start()) LOG_ERROR("Agent data will not be received (UDP listener failed).");
    }
    SkinCache::Start(); // friend avatars (background downloads)
    timeBeginPeriod(1); // 1 ms sleep granularity for the frame limiter
    LOG_INFO("Overlay running. Insert = config, Delete = toggle, hold End = exit. %s", opt_.demo ? "(DEMO)" : "");

    while (!quit_)
    {
        const auto frameStart = Clock::now();
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) quit_ = true;
            TranslateMessage(&msg); // generates WM_CHAR for the text fields
            DispatchMessageW(&msg);
        }
        if (quit_) break;

        if (!replayMode_) sup_.Tick();
        CheckSession();
        if (opt_.simulateLoss != 0 && gfx_.IsGpu())
        {
            const bool down = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
            if (down && !f10Down_) gfx_.SimulateLoss(opt_.simulateLoss == 2);
            f10Down_ = down;
        }
        if (gfx_.IsGpu() && gfx_.WantsLegacy()) SwitchToLegacy();
        if (quit_) break;

        RECT r{};
        const bool haveGame = GameGeometry(r);
        const HWND fg = GetForegroundWindow();
        const HWND mc = (opt_.demo || opt_.screenOverlay) ? nullptr : sup_.GameWindow();
        const bool gameFocused = opt_.demo || opt_.screenOverlay || (mc && fg == mc);
        const bool overlayFocused = fg == hwnd_;
        HandleHotkeys(gameFocused || overlayFocused);

        // User clicked away from the config window (e.g. back into the game): close it so the overlay
        // does not keep swallowing the mouse.
        if (configOpen_ && !overlayFocused && Clock::now() - configOpenedAt_ > 400ms && !opt_.demo && !opt_.
            screenOverlay) CloseConfig();

        const bool active = haveGame && (gameFocused || overlayFocused || configOpen_);
        if (!active)
        {
            SetShown(false);
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT); // idle cheaply while the game is not visible
            continue;
        }
        SetShown(true);
        if (opt_.openConfig && !openedAtStart_)
        {
            openedAtStart_ = true;
            OpenConfig();
        }
        RenderFrame(r, gameFocused);
        Pace(frameStart);
    }
    timeEndPeriod(1);
    SkinCache::Stop();
    replayDriver_.Stop();
    if (!replayMode_)
        if (const std::string link = Src().LinkSummary(); !link.empty()) LOG_INFO("Session end - %s", link.c_str());
    udp_.Stop(); // the producer first, then the recorder (it drains and closes the file)
    recorder_.Stop();
    LOG_INFO("Overlay stopped.");
    return 0;
}

// ------------------------------------------------------------------------------------------------ frame

// Every loop iteration, drawn frame or not: a different game (closed, restarted, another client) or a restarted agent means nothing from before may be
// drawn or extrapolated, and a hidden overlay must not keep the old world either.
void EspOverlay::CheckSession()
{
    if (opt_.demo || replayMode_) return;
    if (sup_.Generation() != lastGeneration_) { lastGeneration_ = sup_.Generation(); ResetSession("the Minecraft process changed"); }
    const uint64_t sessions = Src().Link().sessions;
    if (sessions != lastSessions_) { lastSessions_ = sessions; ResetSession("the agent restarted"); }
}

// The GPU renderer was lost and could not be recovered (GpuRecovery gave up). The window was made for it (no redirection bitmap), so it has to be built
// again for the legacy layered-window renderer - the same step the start-up fallback takes.
void EspOverlay::SwitchToLegacy()
{
    LOG_WARN("Switching to the legacy renderer while running: %s", gfx_.LossSummary().c_str());
    const bool wasClickThrough = clickThrough_;
    suppressQuit_ = true;
    DestroyWindow(hwnd_);
    suppressQuit_ = false;
    hwnd_ = nullptr;
    shown_ = false;
    if (!gfx_.ForceLegacy() || !CreateOverlayWindow())
    {
        LOG_ERROR("Legacy renderer initialisation failed after the GPU was lost - exiting.");
        quit_ = true;
        return;
    }
    if (!wasClickThrough) SetClickThrough(false);   // the config window was open
    LOG_INFO("Renderer: legacy (DIB + UpdateLayeredWindow), after a GPU device loss.");
}

void EspOverlay::ResetSession(const char* why)
{
    LOG_INFO("Session reset (%s): smoothing, snapshot and item state dropped.", why);
    smoother_ = Smoother{};
    snap_ = WorldSnapshot{};
    lastSeq_ = 0;
}

void EspOverlay::RenderFrame(const RECT& r, bool /*gameFocused*/)
{
    const int w = r.right - r.left, h = r.bottom - r.top;
    if (!gfx_.Resize(w, h))
    {
        Sleep(8); // a lost device is retried by GpuRecovery on its own schedule; without this the loop spins at 100% CPU while it waits
        return;
    }
    gfx_.WaitFrame(); // GPU renderer: pace to the compositor here, before sampling any state for this frame
    const auto now = Clock::now();
    const auto cpuStart = now;

    // ---- 1. newest data -> smoother (the UDP thread only ever swaps a pointer; we never block on it) ----
    if (opt_.demo)
    {
        if (now - lastDemoPush_ >= 50ms)
        {
            lastDemoPush_ = now;
            WorldSnapshot d = demo_.Tick();
            d.seq = ++demoSeq_;
            d.arrived = now;
            smoother_.Push(d);
        }
    }
    else if (auto sp = Src().Latest(); sp && sp->seq != lastSeq_)
    {
        lastSeq_ = sp->seq;
        smoother_.Push(*sp);
    }
    WorldSnapshot& snap = snap_;
    if (smoother_.HasData()) smoother_.PredictInto(now, settings_.smoothing, snap);
    else snap = WorldSnapshot{};
    // a replay that is paused or has finished is not "stale": the data is simply not moving on purpose
    const bool stale = !opt_.demo && !(replayMode_ && (replayDriver_.Paused() || replayDriver_.Finished())) &&
        (Src().LastPacketTime() == Clock::time_point{} || now - Src().LastPacketTime() > 1500ms);

    // ---- 2. camera ----
    // Preferred: the game's own camera as read by the agent - exact position (first or third person), exact
    // angles and the exact current FOV (sprint / speed / underwater effects included).
    // Fallback: reconstructed from the local player's eye + angles with the FOV from the config (see Camera.h).
    Camera cam;
    bool haveCam = false, exactCam = false;
    std::string camInfo = "Camera: waiting for data";
    static const char* const kModes[] = {"", "first person", "third person (behind)", "third person (in front)"};
    if (!stale && snap.camera.valid)
    {
        const CameraState& c = snap.camera;
        cam.eye = c.pos;
        cam.yawDeg = c.yawDeg;
        cam.pitchDeg = c.pitchDeg;
        cam.fovYDeg = c.fovValid ? c.fovDeg : presets::Fovs[settings_.fovIdx];
        haveCam = exactCam = true;
        char b[160];
        if (c.fovValid) std::snprintf(b, sizeof b, "Camera: exact from the game - FOV %.1f, %s", c.fovDeg,
                                      kModes[c.mode]);
        else std::snprintf(b, sizeof b, "Camera: exact pose from the game, FOV from config (%d) - %s",
                           presets::Fovs[settings_.fovIdx], kModes[c.mode]);
        camInfo = b;
    }
    else if (!stale && snap.localValid && snap.local.yawValid && snap.local.pitchValid)
    {
        cam.eye = {snap.local.pos.x, snap.local.pos.y + Camera::kEyeHeight, snap.local.pos.z};
        cam.yawDeg = snap.local.yawDeg;
        cam.pitchDeg = snap.local.pitchDeg;
        cam.fovYDeg = presets::Fovs[settings_.fovIdx];
        haveCam = true;
        camInfo =
            "Camera: reconstructed (player pose + config FOV). Turn your head so the agent finds the game camera.";
    }
    if (haveCam)
    {
        cam.w = w;
        cam.h = h;
        cam.Update();
    }
    ui_.SetCameraInfo(camInfo);

    // ---- 3. draw into the off-screen surface ----
    // While the config window is open the whole surface is given alpha 1/255: invisible, but hit-testable,
    // so clicks outside the panel do not leak through into the game.
    gfx_.SetOpacity(presets::OverlayOpacity[settings_.overlayOpacityIdx]);
    gfx_.SetVSync(settings_.vsync);
    gfx_.Begin(configOpen_ ? Col{0, 0, 0, 1.f / 255.f} : Col{0, 0, 0, 0});
    if (visible_ && haveCam)
    {
        renderer_.DrawWorld(gfx_, settings_, cam, snap);
        if (settings_.showMinimap) renderer_.DrawMinimap(gfx_, settings_, cam, snap, w);
    }

    // HUD (top-left)
    ++fpsFrames_;
    if (now - fpsWindowStart_ >= 500ms)
    {
        fps_ = fpsFrames_ / std::chrono::duration<float>(now - fpsWindowStart_).count();
        cpuMs_ = fpsFrames_ > 0 ? cpuAccumMs_ / static_cast<float>(fpsFrames_) : 0.f;
        cpuAccumMs_ = 0;
        fpsFrames_ = 0;
        fpsWindowStart_ = now;
    }
    char line[200];
    const char* state = !visible_ ? "hidden" : (opt_.demo ? "demo" : haveCam ? "active" : "waiting");
    if (exitHolding_)
    {
        const float left = 3.f - std::chrono::duration<float>(now - exitHoldStart_).count();
        std::snprintf(line, sizeof line, "external-esp  |  exiting in %.1fs (release to cancel)", std::max(0.f, left));
        gfx_.Text(line, 10, 8, Font::Ui, Col{1.f, 0.45f, 0.35f, 1.f}, true);
    }
    else
    {
        std::snprintf(line, sizeof line, "external-esp  |  %s  |  %.0f fps  |  %.2f ms cpu  |  %s", state, fps_, cpuMs_,
                      gfx_.IsGpu() ? "gpu" : "gdi");
        gfx_.Text(line, 10, 8, Font::Ui, Col{0.80f, 0.85f, 1.f, 0.95f}, true);
    }
    std::string status = sup_.Status();
    if (const std::string prob = Src().ProtocolProblem(now); !prob.empty() && !replayMode_) status = prob;
    if (elog::Enabled(elog::Level::Debug) && now >= nextDebugLog_)
    {
        nextDebugLog_ = now + std::chrono::seconds(5);
        const auto link = Src().Link();
        LOG_DEBUG("stream: %llu snapshots published, %llu datagrams rejected (%llu auth), numbered %llu / lost %llu / late %llu / dup %llu",
                  static_cast<unsigned long long>(Src().PacketCount()), static_cast<unsigned long long>(Src().RejectedCount()), static_cast<unsigned long long>(Src().AuthFailures()),
                  static_cast<unsigned long long>(link.numbered), static_cast<unsigned long long>(link.lost), static_cast<unsigned long long>(link.reordered),
                  static_cast<unsigned long long>(link.duplicates));
    }
    if (!replayMode_)
        if (const std::string link = Src().LinkSummary(); !link.empty())
        {
            status += "  |  " + link;
            if (now >= nextLinkLog_ && link != lastLinkLog_)
            {
                LOG_WARN("%s", link.c_str());
                lastLinkLog_ = link;
                nextLinkLog_ = now + std::chrono::seconds(10);
            }
        }
    if (replayMode_)
    {
        char rp[160];
        std::snprintf(rp, sizeof rp, "REPLAY %llu/%llu  %.2gx%s", static_cast<unsigned long long>(replayDriver_.Position()),
                      static_cast<unsigned long long>(replayPlayer_.Count()), replayDriver_.Speed(),
                      replayDriver_.Paused() ? "  PAUSED (F7 step)" : (replayDriver_.Finished() ? "  END" : ""));
        status = rp;
    }
    if (haveCam && !opt_.demo) status += exactCam
                                             ? (snap.camera.mode == 1
                                                    ? "  |  cam: exact"
                                                    : "  |  cam: exact, third person")
                                             : "  |  cam: reconstructed";
    if (!opt_.demo && !snap.status.empty() && !stale) status += "  |  " + snap.status;
    if (recorder_.Active())
        status += "  |  REC " + std::to_string(recorder_.Recorded()) +
            (recorder_.Dropped() ? " (" + std::to_string(recorder_.Dropped()) + " dropped)" : std::string());
    lastStatus_ = status;
    gfx_.Text(status, 10, 26, Font::UiSmall, Col{0.62f, 0.67f, 0.80f, 0.9f}, true);
    if (!opt_.demo && !stale && snap.localValid && !haveCam)
        gfx_.Text("camera unavailable: the agent could not find the look-vector method (see esp_layout.json)", 10, 42,
                  Font::UiSmall, Col{1.f, 0.6f, 0.4f, 1.f}, true);
    if (haveCam && visible_ && snap.players.empty() && !opt_.demo)
        gfx_.Text("no other players in range", 10, 42, Font::UiSmall, Col{0.55f, 0.6f, 0.72f, 0.8f}, true);

    if (configOpen_)
    {
        UiInput in = pend_;
        pend_.pressed = pend_.released = pend_.rpressed = false; // edges are one-frame events
        pend_.wheel = 0;
        pend_.chars.clear();
        pend_.keys.clear();
        ui_.Draw(gfx_, in, w, h);
    }

    if (!gfx_.End()) return; // render target lost; Resize() rebuilds it next frame
    gfx_.Present(hwnd_, r.left, r.top, presets::OverlayOpacity[settings_.overlayOpacityIdx]);
    cpuAccumMs_ += std::chrono::duration<float, std::milli>(Clock::now() - cpuStart).count();
    // Legacy renderer: UpdateLayeredWindow has no swap interval, so wait for the compositor's next pass. The GPU
    // renderer is paced by Present() + the swap chain's frame-latency waitable object (Gfx::WaitFrame).
    if (settings_.vsync && !gfx_.IsGpu()) DwmFlush();
}
