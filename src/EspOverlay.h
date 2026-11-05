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
#include "HotkeyRegistry.h"
#include "Hud.h"
#include "PerfStats.h"
#include "Hotkeys.h"
#include "Recorder.h"
#include "Replay.h"
#include "RateApply.h"
#include "Smoother.h"
#include "UdpListener.h"
#include <array>
#include <chrono>
#include <unordered_map>
#include <vector>

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
    hud::Text hudText_;
    hud::Mode lastHudMode_ = hud::Mode::Compact;
    WorldSnapshot rawSnap_; // the "raw" view: a copy of the smoother's newest sample, kept so its vectors keep their capacity
    perf::PerfHistory perfHist_; // the graphs' data; filled only while Settings::showGraphs is on
    Clock::time_point lastFrameT_{};
    bool graphsWereOn_ = false;
    perf::CacheCounters cachePrev_; // the text-cache counters at the last HUD refresh
    Clock::time_point nextHud_{}, hudWinStart_{};
    uint64_t hudPackets_ = 0, hudNumbered_ = 0, hudLost_ = 0;
    float sampleHz_ = 0, lossPct_ = 0;
    RateApplier rate_{120}; // sends a changed AgentHz to the running agent
    int lastSeenHz_ = 120;
    void UpdateAgentRate(Clock::time_point now, bool agentLive);
    bool closeConfigPending_ = false; // Esc in the config window
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
    Clock::time_point nextLinkLog_{}, nextDebugLog_{};
    StreamIngest& Src() { return replayMode_ ? replayIngest_ : udp_.Ingest(); }
    void HandleReplayKeys();
    std::string CopyDiagnostics(); // the config window's "Copy diagnostics" button
    std::string lastStatus_; // the HUD status line of the last frame
    AgentInjector injector_;
    AgentSupervisor sup_;
    Smoother smoother_;
    WorldSnapshot snap_; // reused every frame (see Smoother::PredictInto)
    DemoSource demo_;

    HWND hwnd_ = nullptr;
    // system hotkeys (RegisterHotKey) for the window above; see HotkeyRegistry.h. Declared after hwnd_: it unregisters through it when destroyed.
    struct Win32Registrar : hotkeys::Registrar
    {
        explicit Win32Registrar(HWND& h) : hwnd(h) {}
        int Register(int id, const hotkeys::Binding& b) override;
        void Unregister(int id) override;
        HWND& hwnd;
    } hkRegistrar_{hwnd_};
    hotkeys::Registry hkRegistry_{hkRegistrar_};
    std::vector<int> hotkeyEvents_; // WM_HOTKEY ids since the last loop pass (UI thread only)
    bool shown_ = false, clickThrough_ = true;
    bool visible_ = true; // ESP drawing on/off (toggle hotkey)
    bool configOpen_ = false, quit_ = false, openedAtStart_ = false;
    bool suppressQuit_ = false; // set while the overlay window is deliberately destroyed and recreated (renderer fallback)
    Clock::time_point configOpenedAt_{};
    Clock::time_point exitHoldStart_{};
    bool exitHolding_ = false;
    std::unordered_map<int, bool> prevKeys_;
    std::array<bool, hotkeys::kActionCount> prevDown_{}; // edge detection per hotkey action
    void Perform(hotkeys::Action a);

    unsigned lastGeneration_ = 0;   // AgentSupervisor::Generation() when the data on screen was received
    uint64_t lastSessions_ = 0;     // StreamIngest::Link().sessions
    void CheckSession();
    void ProbeJitter(const Camera& cam, const WorldSnapshot& snap); // feeds the jitter meter with the nearest player's box centre
    void SwitchToLegacy();          // device loss that could not be recovered: new window, legacy renderer
    bool f10Down_ = false;
    void TrackGameWindow(bool shown);          // classification, advice and the z-order watchdog (every 250 ms)
    int windowKind_ = -1;                      // winstate::Kind as int, -1 = not classified yet
    Clock::time_point lastWindowCheck_{}, lastRaise_{};
    unsigned raiseCount_ = 0;
    void ResetSession(const char* why);
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
