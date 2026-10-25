// EspOverlay.h - the transparent, click-through, always-on-top overlay window and the main loop.
#pragma once
#include "AgentInjector.h"
#include "AgentSupervisor.h"
#include "App.h"
#include "Camera.h"
#include "ConfigUi.h"
#include "Demo.h"
#include "EspConfig.h"
#include "EspRenderer.h"
#include "Gfx.h"
#include "Recorder.h"
#include "Replay.h"
#include "Smoother.h"
#include "UdpListener.h"
#include <chrono>
#include <unordered_map>

class EspOverlay
{
public:
    using Clock = std::chrono::steady_clock;

    explicit EspOverlay(const AppOptions& opt);
    ~EspOverlay();
    EspOverlay(const EspOverlay&) = delete;
    EspOverlay& operator=(const EspOverlay&) = delete;

    int Run();

private:
    bool CreateOverlayWindow();
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT OnMessage(HWND, UINT, WPARAM, LPARAM);

    bool GameGeometry(RECT& r) const; // client area of the game window in screen coordinates
    bool KeyEdge(int vk); // rising edge of a globally polled key
    void HandleHotkeys(bool gameOrOverlayFocused);
    void SetClickThrough(bool on);
    void OpenConfig();
    void CloseConfig();
    void ForceForeground(HWND h);
    void SetShown(bool show);
    void RenderFrame(const RECT& r, bool gameFocused);
    void Pace(Clock::time_point frameStart);

    AppOptions opt_;
    Settings settings_;
    Gfx gfx_;
    EspRenderer renderer_;
    ConfigUi ui_;
    Recorder recorder_; // declared BEFORE udp_: the listener (a producer thread) must be destroyed first
    UdpListener udp_;
    // --replay: the recording is published into replayIngest_ by replayDriver_ (a producer thread, so the ingest is declared first
    // and outlives it); the renderer reads whichever source is active through Src().
    StreamIngest replayIngest_;
    ReplayPlayer replayPlayer_;
    ReplayDriver replayDriver_{replayPlayer_, replayIngest_};
    bool replayMode_ = false;
    std::string lastLinkLog_; // last "UDP: n lost ..." line written to the log (it is repeated at most every 10 s and only when it changed)
    Clock::time_point nextLinkLog_{};
    StreamIngest& Src() { return replayMode_ ? replayIngest_ : udp_.Ingest(); }
    void HandleReplayKeys();
    AgentInjector injector_;
    AgentSupervisor sup_;
    Smoother smoother_;
    WorldSnapshot snap_; // reused every frame (see Smoother::PredictInto)
    DemoSource demo_;

    HWND hwnd_ = nullptr;
    bool shown_ = false, clickThrough_ = true;
    bool visible_ = true; // ESP drawing on/off (toggle hotkey)
    bool configOpen_ = false, quit_ = false, openedAtStart_ = false;
    bool suppressQuit_ = false; // set while the overlay window is deliberately destroyed and recreated (renderer fallback)
    Clock::time_point configOpenedAt_{};
    Clock::time_point exitHoldStart_{};
    bool exitHolding_ = false;
    std::unordered_map<int, bool> prevKeys_;

    uint64_t lastSeq_ = 0, demoSeq_ = 0;
    Clock::time_point lastDemoPush_{};
    HWND lastGameWnd_ = nullptr;

    // input accumulated by the window procedure, consumed once per frame
    UiInput pend_;

    // fps counter for the HUD
    Clock::time_point fpsWindowStart_{};
    int fpsFrames_ = 0;
    float fps_ = 0;
    float cpuAccumMs_ = 0, cpuMs_ = 0; // CPU time per frame excluding the wait for the compositor
};
