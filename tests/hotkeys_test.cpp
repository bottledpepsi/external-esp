// hotkeys_test - the binding model: parse / format, problems, conflicts, and how the settings file treats hotkeys.
#include "ConfigSchema.h"
#include "EspConfig.h"
#include "Hotkeys.h"
#include <cstdio>
using namespace hotkeys;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

static Binding P(const char* s) { Binding b; std::string e; if (!Parse(s, b, &e)) std::printf("  (parse '%s': %s)\n", s, e.c_str()); return b; }
static bool Bad(const char* s, const char* part)
{
    Binding b; std::string e;
    return !Parse(s, b, &e) && e.find(part) != std::string::npos;
}

int main()
{
    // ---- parse / format ----
    CHECK(P("Delete").vk == 0x2E && P("Delete").mods == 0);
    CHECK(P("ctrl+shift+f5").mods == (kCtrl | kShift) && P("ctrl+shift+f5").vk == 0x74);
    CHECK(P("Shift + Ctrl + F5") == P("Ctrl+Shift+F5"));       // order and spaces do not matter
    CHECK(P("Alt+Q").vk == 'Q' && P("Alt+Q").mods == kAlt);
    CHECK(P("Win+0x70").mods == kWin && P("Win+0x70").vk == 0x70);
    CHECK(!P("").Bound() && !P("None").Bound() && !P("  none ").Bound());
    CHECK(Format(P("Shift+Ctrl+Alt+Win+F12")) == "Ctrl+Shift+Alt+Win+F12"); // canonical order
    CHECK(Format(P("")) == "" && Format(P("Delete")) == "Delete");
    for (const char* s : {"Delete", "Insert", "End", "F6", "Q", "5", "Ctrl+F6", "Ctrl+Shift+Alt+Win+Tilde", "Alt+[", "Shift+0x41"})
        CHECK(Format(P(s)) == Format(P(Format(P(s)).c_str())));   // format -> parse -> format is stable

    // ---- the key vocabulary: common keys have names (and old hex spellings still parse) ----
    CHECK(P("Space").vk == 0x20 && P("Ctrl+Tab").vk == 0x09 && P("Numpad5").vk == 0x65 && P("Left").vk == 0x25 && P("F13").vk == 0x7C);
    CHECK(P("0x20") == P("Space") && Format(P("0x20")) == "Space");
    CHECK(P("NumpadAdd").vk == 0x6B && Format(P("Ctrl+NumpadDivide")) == "Ctrl+NumpadDivide");

    // ---- rejects, with a reason ----
    CHECK(Bad("Banana", "not a key name"));
    CHECK(Bad("Ctrl+Banana", "not a key name"));
    CHECK(Bad("Ctrl+", "key is missing"));
    CHECK(Bad("Foo+F6", "not a modifier"));
    CHECK(Bad("Ctrl+Ctrl+F6", "twice"));
    CHECK(Bad("Ctrl+0xA2", "modifier key cannot be the key"));  // VK_LCONTROL as the key
    CHECK(Bad("0x10", "modifier key cannot be the key"));       // VK_SHIFT
    CHECK(Bad("+", "missing"));

    // ---- problems (parse fine, cannot work) ----
    CHECK(Problem(P("Alt+F4")).find("Windows") != std::string::npos);
    CHECK(Problem(P("Alt+Tab")).size() > 0 && Problem(P("Win+L")).size() > 0 && Problem(P("Ctrl+Alt+Delete")).size() > 0 && Problem(P("Ctrl+Esc")).size() > 0);
    CHECK(Problem(P("Ctrl+Alt+F4")).empty());                    // only the bare Alt combination is Windows'
    CHECK(Problem(P("F6")).empty() && Problem(P("Ctrl+Shift+F5")).empty() && Problem(P("")).empty());

    // ---- the table ----
    {
        const Table d = Defaults();
        CHECK(d[0] == P("Delete") && d[1] == P("Insert") && d[2] == P("End"));
        for (int i = 3; i < kActionCount; ++i) CHECK(!d[static_cast<size_t>(i)].Bound());   // the new ones start unbound: no key of the game is taken
        CHECK(FindConflicts(d).empty());
        for (int i = 0; i < kActionCount; ++i) CHECK(Problem(d[static_cast<size_t>(i)]).empty());
        CHECK(Def(Action::ToggleOverlay).required && Def(Action::OpenConfig).required && !Def(Action::TogglePlayers).required && Def(Action::HoldExit).hold);
        Table t = d;
        t[3] = P("F6"); t[4] = P("F6"); t[5] = P("Ctrl+F6");
        const auto c = FindConflicts(t);
        CHECK(c.size() == 1 && c[0].first == Action::TogglePlayers && c[0].second == Action::ToggleItems);
        CHECK(UsedBy(t, P("F6"), Action::ToggleItems) == Action::TogglePlayers);
        CHECK(UsedBy(t, P("F6"), Action::TogglePlayers) == Action::ToggleItems);
        CHECK(UsedBy(t, P("F7"), Action::Count) == Action::Count && UsedBy(t, Binding{}, Action::Count) == Action::Count);
        // every action has a distinct key name in the settings file
        for (int i = 0; i < kActionCount; ++i)
            for (int j = i + 1; j < kActionCount; ++j) CHECK(std::string(Def(ActionAt(i)).key) != Def(ActionAt(j)).key);
    }

    // ---- the settings file ----
    {
        // old files (a plain key name) keep working; new keys default to unbound
        auto r = configschema::LoadText(R"({"HotkeyToggle":"F6","HotkeyConfig":"Home","HotkeyExit":"F12"})");
        CHECK(r.Warnings() == 0);
        Settings s; s.Apply(r.cfg);
        CHECK(s.hotkey[0] == P("F6") && s.hotkey[1] == P("Home") && s.hotkey[2] == P("F12") && !s.hotkey[3].Bound());
        // with modifiers; round trip through Build()
        r = configschema::LoadText(R"({"HotkeyTogglePlayers":"Ctrl+Shift+P","HotkeyNextProfile":"alt+n"})");
        CHECK(r.Warnings() == 0);
        s.Apply(r.cfg);
        CHECK(s.hotkey[3] == P("Ctrl+Shift+P") && s.hotkey[7] == P("Alt+N"));
        const std::string text = s.Build().ToJson();
        CHECK(text.find("\"HotkeyTogglePlayers\": \"Ctrl+Shift+P\"") != std::string::npos && text.find("\"HotkeyNextProfile\": \"Alt+N\"") != std::string::npos);
        CHECK(configschema::LoadText(text).Warnings() == 0);
        // bad ones: a reason and the default
        r = configschema::LoadText(R"({"HotkeyToggle":"Banana","HotkeyConfig":"","HotkeyExit":"Alt+F4","HotkeyToggleItems":"Ctrl+","HotkeyToggleTracers":5})");
        CHECK(r.Warnings() == 5);
        CHECK(r.cfg.HotkeyToggle == "Delete" && r.cfg.HotkeyConfig == "Insert" && r.cfg.HotkeyExit == "End" && r.cfg.HotkeyToggleItems.empty());
        bool saidEmpty = false, saidWindows = false;
        for (const auto& i : r.issues) { saidEmpty |= i.message.find("cannot be left empty") != std::string::npos; saidWindows |= i.message.find("belong to Windows") != std::string::npos; }
        CHECK(saidEmpty && saidWindows);
        // an optional hotkey may be empty, "None" too
        CHECK(configschema::LoadText(R"({"HotkeyTogglePlayers":"","HotkeyToggleItems":"None"})").Warnings() == 0);
        // conflicts: the earlier action keeps the key; an optional later one is unbound, a required later one gets its default back
        r = configschema::LoadText(R"({"HotkeyTogglePlayers":"F8","HotkeyToggleItems":"F8"})");
        CHECK(r.Warnings() == 1 && r.cfg.HotkeyTogglePlayers == "F8" && r.cfg.HotkeyToggleItems.empty());
        r = configschema::LoadText(R"({"HotkeyToggle":"Insert"})");   // collides with the default of OpenConfig
        CHECK(r.Warnings() == 1 && r.cfg.HotkeyToggle == "Delete" && r.cfg.HotkeyConfig == "Insert");   // both back to their defaults
        {
            Table t{};
            for (int i = 0; i < kActionCount; ++i) Parse(r.cfg.HotkeyText(ActionAt(i)), t[static_cast<size_t>(i)]);
            CHECK(FindConflicts(t).empty());                       // whatever it did, no two actions share a key afterwards
        }
        r = configschema::LoadText(R"({"HotkeyConfig":"Delete"})");  // OpenConfig on ToggleOverlay's default
        { Table t{}; for (int i = 0; i < kActionCount; ++i) Parse(r.cfg.HotkeyText(ActionAt(i)), t[static_cast<size_t>(i)]); CHECK(FindConflicts(t).empty()); }
    }

    if (fails) std::printf("%d failure(s)\n", fails); else std::puts("hotkeys_test: all passed");
    return fails ? 1 : 0;
}
