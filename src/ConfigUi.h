// ConfigUi.h - the multi-tab configuration window (port of the C# config window, new dark theme).
//
// Immediate-mode: every frame Draw() lays the rows out top to bottom and the same call that draws a
// widget also hit-tests it against this frame's input, so layout and click handling can never
// disagree (the C# tool kept two parallel code paths that had to be edited in lock-step).
// Layout is the original's: 480 px panel, title bar (drag to move), 7 tabs
// GENERAL | PLAYERS | TRACERS | ITEMS | MINIMAP | PROFILES | HOTKEYS, 24 px rows, value column at +300.
#pragma once
#include "EspConfig.h"
#include "Gfx.h"
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
};

class ConfigUi
{
public:
    explicit ConfigUi(Settings& s) : s_(s)
    {
    }

    void Draw(Gfx& g, const UiInput& in, int winW, int winH);
    void SetCameraInfo(const std::string& s) { cameraInfo_ = s; }
    // "Copy diagnostics" (GENERAL tab): the handler builds the report, puts it on the clipboard and returns the one-line result to show.
    void SetDiagnosticsHandler(std::function<std::string()> h) { diagnostics_ = std::move(h); }
    // shown in GENERAL (exact / reconstructed, FOV, perspective)
    bool CapturingKeys() const { return rebindActive_ || friendFocus_ || profileFocus_; }

    void ResetFocus()
    {
        friendFocus_ = profileFocus_ = rebindActive_ = false;
        dragging_ = false;
    }

private:
    // widgets (draw + hit-test + apply in one call)
    bool Hot(float x, float y, float w, float h) const;
    bool CycleRow(const std::string& label, int& idx, int count, const std::string& valueText);
    bool ToggleRow(const std::string& label, bool& v);
    void InfoRow(const std::string& text, bool warn = false);
    void Section(const std::string& title);
    bool Button(const std::string& label, float x, float w, float h = 20.f, const Col* bg = nullptr);
    bool Input(const std::string& text, float x, float w, bool focused, const char* placeholder);

    void TabGeneral();
    void TabPlayers();
    void TabTracers();
    void TabItems();
    void TabMinimap();
    void TabProfiles();
    void TabHotkeys();
    void HotkeyRow(const std::string& label, int& vk, int slot);
    void Save() { s_.Save(); }

    Settings& s_;
    Gfx* g_ = nullptr;
    const UiInput* in_ = nullptr;
    float px_ = 0, py_ = 0; // panel origin this frame
    float y_ = 0; // layout cursor
    bool clipOn_ = false;
    float clipX_ = 0, clipY_ = 0, clipW_ = 0, clipH_ = 0;

    int tab_ = 0;
    float scroll_[7] = {0, 0, 0, 0, 0, 0, 0};
    float contentH_[7] = {0, 0, 0, 0, 0, 0, 0};
    bool dragging_ = false;
    float dragOffX_ = 0, dragOffY_ = 0;

    std::function<std::string()> diagnostics_;
    std::string diagnosticsNote_;
    std::string cameraInfo_;
    std::string friendInput_, profileInput_;
    bool friendFocus_ = false, profileFocus_ = false;
    bool rebindActive_ = false;
    int rebindSlot_ = 0; // 1 toggle, 2 config, 3 exit
    long long frame_ = 0;
};
