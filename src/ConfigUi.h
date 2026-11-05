// ConfigUi.h - the multi-tab configuration window (port of the C# config window, new dark theme).
//
// Immediate-mode: every frame Draw() lays the rows out top to bottom and the same call that draws a
// widget also hit-tests it against this frame's input, so layout and click handling can never
// disagree (the C# tool kept two parallel code paths that had to be edited in lock-step).
// Layout is the original's: 480 px panel, title bar (drag to move), 8 tabs
// GENERAL | PLAYERS | TRACERS | ITEMS | MINIMAP | PROFILES | HOTKEYS | ADVANCED, 24 px rows, value column at +300.
#pragma once
#include "EspConfig.h"
#include "Gfx.h"
#include "HotkeyRegistry.h"
#include "Smoother.h"
#include "Tunables.h"
#include "UiNav.h"
#include <functional>
#include <string>
#include <vector>

struct UiInput
{
    float mx = 0, my = 0; // cursor, overlay-client coordinates
    bool down = false; // left button held
    bool pressed = false, released = false; // left button edges this frame
    bool rpressed = false; // right button edge (cycles rows backwards)
    float wheel = 0; // notches this frame (+ = up)
    std::string chars; // typed printable ASCII this frame
    std::vector<int> keys; // virtual-key presses this frame (WM_KEYDOWN)
    bool shift = false, ctrl = false, alt = false, win = false; // modifier state when the frame was built
};

class ConfigUi
{
public:
    explicit ConfigUi(Settings& s) : s_(s)
    {
    }

    void Draw(Gfx& g, const UiInput& in, int winW, int winH);
    void SetCameraInfo(const std::string& s) { cameraInfo_ = s; }
    // one line under the agent-rate slider (what happened to the last change), set every frame by the overlay
    // the registry that says which hotkeys could not be registered with the system (read while drawing the HOTKEYS tab)
    void SetHotkeyRegistry(const hotkeys::Registry* r) { hkRegistry_ = r; }
    void SetRateNote(const std::string& s, bool warn)
    {
        rateNote_ = s;
        rateWarn_ = warn;
    }
    // numbers for the ADVANCED tab's live readout (set every frame by the overlay)
    void SetLive(const SmootherStats& st, int screenH, double fovYDeg)
    {
        liveStats_ = st;
        liveH_ = screenH;
        liveFov_ = fovYDeg;
    }
    // "Copy diagnostics" (GENERAL tab): the handler builds the report, puts it on the clipboard and returns the one-line result to show.
    void SetDiagnosticsHandler(std::function<std::string()> h) { diagnostics_ = std::move(h); }
    // shown in GENERAL (exact / reconstructed, FOV, perspective)
    bool CapturingKeys() const { return rebindActive_ || friendFocus_ || profileFocus_ || itemFocus_; }
    // Esc was pressed with nothing waiting for input: the overlay closes the window (taken once).
    bool TakeCloseRequest()
    {
        const bool r = closeRequested_;
        closeRequested_ = false;
        return r;
    }

    void ResetFocus()
    {
        friendFocus_ = profileFocus_ = itemFocus_ = rebindActive_ = false;
        dragging_ = false;
        sliderDrag_ = -1;
        nav_.OnMouse();
    }

private:
    // widgets (draw + hit-test + apply in one call)
    bool Hot(float x, float y, float w, float h) const;
    bool CycleRow(const std::string& label, int& idx, int count, const std::string& valueText);
    bool ToggleRow(const std::string& label, bool& v);
    void InfoRow(const std::string& text, bool warn = false);
    void Section(const std::string& title);
    bool Button(const std::string& label, float x, float w, float h = 20.f, const Col* bg = nullptr);
    // keyboard: registers the control at the layout cursor as a stop; true when focused (draws the ring); `act` receives the pending key action
    bool NavStop(float x, float w, float h, UiNav::Act& act);
    bool Input(const std::string& text, float x, float w, bool focused, const char* placeholder);

    void TabGeneral();
    void TabPlayers();
    void TabTracers();
    void TabItems();
    void TabMinimap();
    void TabProfiles();
    void TabHotkeys();
    void TabAdvanced();
    // one row of the ADVANCED tab: label, slider, value, RESET (only when changed) and the help line. Returns true when the value changed.
    bool SliderRow(tunables::Id id);
    void HotkeyRow(hotkeys::Action a);
    void Save() { s_.RequestSave(); } // written by the overlay loop once the changes stop (EspConfig.h)

    Settings& s_;
    Gfx* g_ = nullptr;
    const UiInput* in_ = nullptr;
    float px_ = 0, py_ = 0; // panel origin this frame
    float y_ = 0; // layout cursor
    bool clipOn_ = false;
    float clipX_ = 0, clipY_ = 0, clipW_ = 0, clipH_ = 0;

    UiNav nav_;
    int stopsLast_[UiNav::kMaxTabs] = {};
    float lastMx_ = -1, lastMy_ = -1;
    bool closeRequested_ = false;
    int tab_ = 0;
    float scroll_[8] = {};
    float contentH_[8] = {};
    const hotkeys::Registry* hkRegistry_ = nullptr;
    std::string hotkeyNote_; // why the last rebind was refused (in use, reserved by Windows)
    std::string rateNote_;
    bool rateWarn_ = false;
    int sliderDrag_ = -1; // tunables::Id being dragged with the mouse, -1 none
    SmootherStats liveStats_;
    int liveH_ = 0;
    double liveFov_ = 70;
    bool dragging_ = false;
    float dragOffX_ = 0, dragOffY_ = 0;

    std::function<std::string()> diagnostics_;
    std::string diagnosticsNote_;
    std::string cameraInfo_;
    std::string friendInput_, profileInput_, itemInput_; // itemInput_: a name pattern for the item filter
    bool friendFocus_ = false, profileFocus_ = false, itemFocus_ = false;
    bool rebindActive_ = false;
    int rebindSlot_ = 0; // hotkeys::Action + 1
    long long frame_ = 0;
};
