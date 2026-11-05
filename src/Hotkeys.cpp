#include "Hotkeys.h"
#include "Presets.h"
#include <algorithm>
#include <cctype>

namespace hotkeys
{
    namespace
    {
        const ActionDef kDefs[kActionCount] = {
            {Action::ToggleOverlay, "HotkeyToggle", "Toggle overlay", "Delete", true, false},
            {Action::OpenConfig, "HotkeyConfig", "Open config", "Insert", true, false},
            {Action::HoldExit, "HotkeyExit", "Hold to exit (3 s)", "End", false, true},
            {Action::TogglePlayers, "HotkeyTogglePlayers", "Toggle players", "", false, false},
            {Action::ToggleItems, "HotkeyToggleItems", "Toggle items", "", false, false},
            {Action::ToggleTracers, "HotkeyToggleTracers", "Toggle tracers", "", false, false},
            {Action::ToggleMinimap, "HotkeyToggleMinimap", "Toggle minimap", "", false, false},
            {Action::NextProfile, "HotkeyNextProfile", "Next profile", "", false, false},
            {Action::CycleHud, "HotkeyCycleHud", "Cycle status text", "", false, false},
            {Action::CycleView, "HotkeyCycleView", "Cycle raw / smoothed view", "", false, false},
            {Action::ToggleGraphs, "HotkeyToggleGraphs", "Toggle performance graphs", "", false, false},
            {Action::ToggleEntities, "HotkeyToggleEntities", "Toggle mobs / entities", "", false, false},
            {Action::ToggleHostile, "HotkeyToggleHostile", "Toggle hostile mobs", "", false, false},
            {Action::TogglePassive, "HotkeyTogglePassive", "Toggle passive mobs", "", false, false},
            {Action::ToggleNeutral, "HotkeyToggleNeutral", "Toggle neutral mobs", "", false, false},
            {Action::ToggleProjectiles, "HotkeyToggleProjectiles", "Toggle projectiles", "", false, false},
            {Action::ToggleXp, "HotkeyToggleXp", "Toggle experience orbs", "", false, false},
            {Action::ClearDeathMarker, "HotkeyClearDeath", "Clear the death marker", "", false, false},
        };

        std::string Lower(std::string s)
        {
            for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }
        std::string Trim(const std::string& s)
        {
            const size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
            return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
        }
    } // namespace

    const ActionDef& Def(Action a) { return kDefs[static_cast<int>(a)]; }

    Table Defaults()
    {
        Table t{};
        for (int i = 0; i < kActionCount; ++i) Parse(kDefs[i].defaultText, t[static_cast<size_t>(i)]);
        return t;
    }

    bool IsModifierVk(int vk)
    {
        return vk == 0x10 || vk == 0x11 || vk == 0x12 || vk == 0xA0 || vk == 0xA1 || vk == 0xA2 || vk == 0xA3 || vk == 0xA4 || vk == 0xA5 || vk == 0x5B ||
            vk == 0x5C;
    }

    std::string Format(const Binding& b)
    {
        if (!b.Bound()) return {};
        std::string s;
        if (b.mods & kCtrl) s += "Ctrl+";
        if (b.mods & kShift) s += "Shift+";
        if (b.mods & kAlt) s += "Alt+";
        if (b.mods & kWin) s += "Win+";
        return s + presets::VkToName(b.vk);
    }

    bool Parse(const std::string& text, Binding& out, std::string* error)
    {
        auto fail = [&](const std::string& m)
        {
            if (error) *error = m;
            return false;
        };
        const std::string t = Trim(text);
        if (t.empty() || Lower(t) == "none")
        {
            out = Binding{};
            return true;
        }
        // split at '+', keeping a trailing "+" key ("Ctrl++" = Ctrl and the plus key) out of scope: the plus key is written "=" like on the keyboard
        std::vector<std::string> parts;
        size_t start = 0;
        for (size_t i = 0; i <= t.size(); ++i)
            if (i == t.size() || t[i] == '+')
            {
                parts.push_back(Trim(t.substr(start, i - start)));
                start = i + 1;
            }
        unsigned mods = kNone;
        for (size_t i = 0; i + 1 < parts.size(); ++i)
        {
            const std::string m = Lower(parts[i]);
            unsigned bit = 0;
            if (m == "ctrl" || m == "control") bit = kCtrl;
            else if (m == "shift") bit = kShift;
            else if (m == "alt") bit = kAlt;
            else if (m == "win" || m == "windows") bit = kWin;
            else if (parts[i].empty()) return fail("a modifier is missing before a '+'");
            else return fail("'" + parts[i] + "' is not a modifier (Ctrl, Shift, Alt, Win)");
            if (mods & bit) return fail("'" + parts[i] + "' appears twice");
            mods |= bit;
        }
        const std::string key = parts.back();
        if (key.empty()) return fail("a key is missing after the last '+'");
        if (!presets::IsKnownKeyName(key)) return fail("'" + key + "' is not a key name (examples: Delete, F6, Q, 0x2D)");
        const int vk = presets::NameToVk(key);
        if (IsModifierVk(vk)) return fail("a modifier key cannot be the key itself (use e.g. Ctrl+F6)");
        out.mods = mods;
        out.vk = vk;
        return true;
    }

    std::string Problem(const Binding& b)
    {
        if (!b.Bound()) return {};
        if (IsModifierVk(b.vk)) return "a modifier key cannot be the key itself";
        // combinations Windows (or every program) keeps: they cannot be registered, or would break the machine's own shortcuts
        if ((b.mods & kAlt) && !(b.mods & ~kAlt) && (b.vk == 0x73 /*F4*/ || b.vk == 0x09 /*Tab*/ || b.vk == 0x1B /*Esc*/ || b.vk == 0x20 /*Space*/))
            return "Alt+F4, Alt+Tab, Alt+Esc and Alt+Space belong to Windows";
        if ((b.mods & kCtrl) && (b.mods & kAlt) && b.vk == 0x2E) return "Ctrl+Alt+Delete belongs to Windows";
        if ((b.mods & kWin) && b.vk == 0x4C) return "Win+L locks the computer";
        if ((b.mods & kCtrl) && !(b.mods & ~kCtrl) && b.vk == 0x1B) return "Ctrl+Esc opens the Start menu";
        return {};
    }

    std::vector<Conflict> FindConflicts(const Table& t)
    {
        std::vector<Conflict> out;
        for (int i = 0; i < kActionCount; ++i)
            for (int j = i + 1; j < kActionCount; ++j)
                if (t[static_cast<size_t>(i)].Bound() && t[static_cast<size_t>(i)] == t[static_cast<size_t>(j)]) out.push_back({ActionAt(i), ActionAt(j)});
        return out;
    }

    Action UsedBy(const Table& t, const Binding& b, Action except)
    {
        if (b.Bound())
            for (int i = 0; i < kActionCount; ++i)
                if (ActionAt(i) != except && t[static_cast<size_t>(i)] == b) return ActionAt(i);
        return Action::Count;
    }
} // namespace hotkeys
