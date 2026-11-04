// EspConfig.h - port of EspConfig.cs (serialized model) + the runtime "Settings" that the UI edits.
//
// EspConfig  : human-readable values (strings, real numbers) - exactly the JSON keys of the C# tool,
//              so an existing esp_config.json / profile loads unchanged. A few keys were ADDED
//              (Fov, VSync, Smoothing, ShowHealth); the C# tool ignores unknown keys.
// Settings   : the same values as preset INDICES (cycle rows in the UI step through the presets),
//              with Apply(EspConfig) / Build() converting between the two, like ApplyConfig/BuildConfig.
#pragma once
#include "Hotkeys.h"
#include "Hud.h"
#include "Json.h"
#include <chrono>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

struct EspConfig
{
    // Hotkeys
    // A hotkey is a key, optionally with Ctrl / Shift / Alt / Win: "F6", "Ctrl+Shift+F5". "" = not bound (allowed except for the first two).
    std::string HotkeyToggle = "Delete", HotkeyConfig = "Insert", HotkeyExit = "End";
    std::string HotkeyTogglePlayers, HotkeyToggleItems, HotkeyToggleTracers, HotkeyToggleMinimap, HotkeyNextProfile, HotkeyCycleHud;
    std::string& HotkeyText(hotkeys::Action a); // the field of an action
    // GENERAL
    std::string DistanceFormat = "Meters"; // Meters | Kilometers | Off
    int MaxLabelDist = -1; // -1 = Unlimited
    bool TextShadow = true;
    // PLAYERS
    bool ShowPlayers = true, ShowPlayerNames = true;
    std::string BoxStyle = "Sharp"; // Rounded | Sharp | Corner-only | Skeleton
    float CornerRadius = 2.f;
    int BoxThickness = 1;
    std::string BoxColorMode = "Static"; // Static | Gradient
    int NameSize = 13;
    float BoxFillOpacity = 0.f;
    // TRACERS
    bool ShowTracers = true;
    std::string TracerOrigin = "Bottom"; // Bottom | Center
    std::string TracerColor = "Dynamic"; // Dynamic | White
    float TracerOpacity = 0.75f;
    int TracerThickness = 1;
    std::string TracerTarget = "Center"; // Feet | Center | Head
    bool TracerFade = true;
    // ITEMS
    bool ShowItems = true, ShowItemNames = true, ShowItemCount = true;
    float ItemOpacity = 0.75f;
    bool HideJunk = false;
    // FILTERING
    int MaxPlayerDist = -1; // -1 = Unlimited
    // MINIMAP
    bool ShowMinimap = true;
    int MinimapSize = 160, MinimapRange = 100;
    // OVERLAY
    float OverlayOpacity = 1.0f;
    int FpsCap = 0; // 0 = Unlimited
    // CONFIG WINDOW position
    float ConfigX = 16.f, ConfigY = 38.f;
    // ---- additions in the C++ port ----
    int Fov = 70; // vertical FOV in degrees (must match Minecraft's FOV option)
    bool VSync = true; // pace presentation to the display refresh (DwmFlush)
    bool Smoothing = true; // extrapolate between 20-60 Hz agent updates
    bool ShowHealth = true; // health bar next to the box (when the agent reports health)
    bool ShowItemEnchants = true; // list enchantments under an item's name
    bool ShowItemTags = false; // list the stack's raw tags (NBT / components) under the name
    std::string HudMode = "Compact"; // the status text top-left: Off | Compact | Detailed (hud::Mode)
    bool LabelDeclutter = true; // names and distances move out of each other's way (LabelLayout.h); false = the old, overlapping labels
    double LabelFadePct = 30; // names fade out over the last N % of MaxLabelDist; 0 = hard cut (Tunables.h)

    // ADVANCED tab (see Tunables.h for the ranges): smoothing, agent rate, minimum marker sizes
    double SmootherBaselineMs = 12, SmootherBlendTauMs = 10, SmootherMaxAheadMs = 50, SmootherTickMs = 50;
    int AgentHz = 120;
    double ItemMinPx = 6, PlayerMinPx = 2;

    // Keys of the file that this build does not know, as their raw JSON text (key -> value). Kept through load -> edit -> save, so a file written
    // by a newer build (or annotated by hand) does not lose them. See ConfigSchema.h.
    std::map<std::string, std::string> Unknown;

    std::string ToJson() const; // writes "Version" first and the unknown keys last
    // Reads the keys it knows (missing or wrong-typed ones keep their defaults) and collects the others into Unknown. It does not validate:
    // configschema::Load() migrates and checks first, and is what the settings and profile loaders call.
    static EspConfig FromJson(const json::Object& o);
};

struct Settings
{
    hotkeys::Table hotkey = hotkeys::Defaults(); // indexed by hotkeys::Action
    // GENERAL
    int distFmtIdx = 0;
    float labelDist = 3.4028235e38f; // meters; FLT_MAX = unlimited (presets::kUnlimited)
    bool textShadow = true;
    // PLAYERS
    bool showPlayers = true, showPlayerNames = true;
    int boxStyleIdx = 1, cornerRadiusIdx = 0, boxThickIdx = 0, playerColorIdx = 0, nameSizeIdx = 1, boxFillIdx = 0;
    // TRACERS
    bool showTracers = true;
    int tracerOriginIdx = 0, tracerColorIdx = 0, tracerOpacityIdx = 2, tracerThickIdx = 0, tracerTargetIdx = 1;
    bool tracerFade = true;
    // ITEMS
    bool showItems = true, showItemNames = true, showItemCount = true, hideJunk = false;
    int itemOpacityIdx = 2;
    // FILTERING
    int maxPlayerDistIdx = 3;
    // MINIMAP
    bool showMinimap = true;
    int minimapSizeIdx = 1, minimapRangeIdx = 1;
    // OVERLAY
    int overlayOpacityIdx = 0, fpsCap = 2;
    float cfgX = 16.f, cfgY = 38.f;
    // additions
    int fovIdx = 1;
    bool vsync = true, smoothing = true, showHealth = true;
    bool showItemEnchants = true, showItemTags = false;

    void Apply(const EspConfig& c);
    EspConfig Build() const;

    // ADVANCED tab values (Tunables.h reads and writes them)
    double smBaselineMs = 12, smBlendTauMs = 10, smMaxAheadMs = 50, smTickMs = 50;
    int agentHz = 120;
    float itemMinPx = 6, playerMinPx = 2;
    int hudMode = 1; // hud::Mode
    bool labelDeclutter = true;
    int labelFadePct = 30;

    // Keys of the loaded file this build does not know (kept so the next Save writes them back), and what Load() had to correct or migrate.
    std::map<std::string, std::string> unknownKeys;
    std::vector<std::string> loadNotes; // one line per warning of the last Load(); empty = the file was fine

    // esp_config.json in ConfigDir() (%APPDATA%\external-esp). Load() recovers from a damaged file (ConfigStore.h); Save() is crash-safe
    // (temp file + flush + replace, last good copy kept as esp_config.json.bak), never throws, and returns false when the disk refused.
    void Load();
    bool Save() const;
    // For the UI: "something changed". The file is written once the changes stop for `quiet` (a slider drag is one save), at the latest 3 s
    // after the first unsaved change; Flush() writes now (closing the config window, exit).
    void RequestSave();
    bool SaveIfDue(std::chrono::steady_clock::time_point now, std::chrono::milliseconds quiet = std::chrono::milliseconds(400));
    bool Flush();
    bool SavePending() const { return saveDirty_; }
    static std::filesystem::path ConfigPath();
    static int StoredAgentHz(); // AgentHz from the settings file without applying anything (0 when there is no usable file); used before the overlay exists

private:
    bool saveDirty_ = false;
    std::chrono::steady_clock::time_point saveRequestedAt_{}, lastChangeAt_{};
};
