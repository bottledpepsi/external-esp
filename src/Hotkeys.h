// Hotkeys.h - what a hotkey is (a key plus Ctrl / Shift / Alt / Win), how it is written in the settings file ("Ctrl+Shift+F5"), which actions
// exist and their defaults, and how conflicts are found. Portable: no Windows headers (the modifier bits equal Win32's MOD_* on purpose).
#pragma once
#include <array>
#include <string>
#include <vector>

namespace hotkeys
{
    enum Mod : unsigned { kNone = 0, kAlt = 1, kCtrl = 2, kShift = 4, kWin = 8 };

    struct Binding
    {
        unsigned mods = kNone;
        int vk = 0; // 0 = unbound
        bool Bound() const { return vk != 0; }
        bool operator==(const Binding& o) const { return mods == o.mods && vk == o.vk; }
        bool operator!=(const Binding& o) const { return !(*this == o); }
    };

    enum class Action
    {
        ToggleOverlay,
        OpenConfig,
        HoldExit,
        TogglePlayers,
        ToggleItems,
        ToggleTracers,
        ToggleMinimap,
        NextProfile,
        Count
    };
    inline constexpr int kActionCount = static_cast<int>(Action::Count);

    struct ActionDef
    {
        Action id;
        const char* key; // key in esp_config.json
        const char* label; // shown in the HOTKEYS tab
        const char* defaultText; // "Delete", "" = unbound by default
        bool required; // may not be unbound (without them there is no way to show or open the window)
        bool hold; // fires after being held (HoldExit), not on press: cannot be a system hotkey
    };
    const ActionDef& Def(Action a);
    inline Action ActionAt(int i) { return static_cast<Action>(i); }

    using Table = std::array<Binding, kActionCount>;
    Table Defaults();

    std::string Format(const Binding& b); // "Ctrl+Shift+F5"; "" for unbound
    // "Ctrl+Shift+F5", "ctrl + f5", "Delete", "" or "None" (unbound). On failure returns false and `error` says why.
    bool Parse(const std::string& text, Binding& out, std::string* error = nullptr);

    // Why this binding cannot work as a hotkey, or "" when it is fine. (A modifier alone, combinations Windows keeps for itself.)
    std::string Problem(const Binding& b);

    struct Conflict
    {
        Action first, second; // two actions with the same binding
    };
    std::vector<Conflict> FindConflicts(const Table& t);
    // The action that already uses `b` (ignoring `except`), or Count.
    Action UsedBy(const Table& t, const Binding& b, Action except);

    bool IsModifierVk(int vk); // Shift, Ctrl, Alt, Win (left/right/generic)
} // namespace hotkeys
