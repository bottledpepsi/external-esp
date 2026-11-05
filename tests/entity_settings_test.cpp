// entity_settings_test - the entity display's settings: defaults, the file, bad values, palette, and the hotkeys.
#include "ConfigSchema.h"
#include "EspConfig.h"
#include "Hotkeys.h"
#include "Presets.h"
#include <cstdio>
#include <set>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

int main()
{
    // ---- defaults: nothing changes for an existing user ----
    {
        Settings s;
        CHECK(!s.showEntities && s.entLabels && s.entDistIdx == 1);
        const bool wantShow[kEntityKindCount] = {false, true, true, true, true, true}; // Other is hidden by default
        for (int k = 0; k < kEntityKindCount; ++k) CHECK(s.entShow[k] == wantShow[k]);
        CHECK(presets::EntityPaletteNames[static_cast<size_t>(s.entColor[static_cast<int>(EntityKind::Hostile)])] == "Red");
        CHECK(presets::EntityPaletteNames[static_cast<size_t>(s.entColor[static_cast<int>(EntityKind::Other)])] == "Gray");
        CHECK(presets::EntityDistLabels[static_cast<size_t>(s.entDistIdx)] == "64m" && presets::EntityDist[1] == 64.f);
        // Build/Apply of the defaults is the identity: the file written for a fresh Settings reads back as the same settings
        Settings t;
        t.Apply(configschema::LoadText(s.Build().ToJson()).cfg);
        CHECK(!t.showEntities && t.entDistIdx == 1);
        for (int k = 0; k < kEntityKindCount; ++k) CHECK(t.entShow[k] == s.entShow[k] && t.entColor[k] == s.entColor[k]);
    }

    // ---- the file ----
    {
        Settings s;
        s.showEntities = true;
        s.entLabels = false;
        s.entDistIdx = 3;
        s.entShow[0] = true;
        s.entShow[1] = false;
        for (int k = 0; k < kEntityKindCount; ++k) s.entColor[k] = (k * 2 + 1) % static_cast<int>(presets::EntityPaletteNames.size());
        const auto back = configschema::LoadText(s.Build().ToJson());
        CHECK(back.Warnings() == 0);
        Settings t;
        t.Apply(back.cfg);
        CHECK(t.showEntities && !t.entLabels && t.entDistIdx == 3 && t.entShow[0] && !t.entShow[1]);
        for (int k = 0; k < kEntityKindCount; ++k) CHECK(t.entColor[k] == s.entColor[k]);
        CHECK(back.cfg.EntityMaxDist == "Unlimited" && back.cfg.EntityColor[1] == presets::EntityPaletteNames[3]);
    }
    {
        const auto old = configschema::LoadText(R"({"Version":1})");
        CHECK(old.Warnings() == 0 && !old.cfg.ShowEntities && old.cfg.EntityMaxDist == "64m");
        const auto bad = configschema::LoadText(R"({"ShowEntities":1,"EntityMaxDist":"far","ColorHostile":"Purple","ShowPassive":"yes","ColorXp":7})");
        CHECK(bad.Warnings() == 5 && !bad.cfg.ShowEntities && bad.cfg.EntityMaxDist == "64m" && bad.cfg.EntityColor[1] == "Red" && bad.cfg.EntityShow[2]);
        Settings t;
        t.Apply(bad.cfg);
        CHECK(t.entColor[static_cast<int>(EntityKind::Hostile)] == 0 && t.entDistIdx == 1);
        // a config that skips the new keys altogether (another program wrote it) is the defaults
        EspConfig c;
        c.EntityColor[1] = "nonsense"; // a value that slipped past the schema (hand-built config): Apply falls back to the default color
        Settings u;
        u.Apply(c);
        CHECK(u.entColor[1] == 0);
        c.EntityMaxDist = "";
        u.Apply(c);
        CHECK(u.entDistIdx == 1);
    }
    CHECK(presets::EntityPaletteIndex("Cyan", -1) == 4 && presets::EntityPaletteIndex("cyan", -1) == -1 && presets::EntityPaletteIndex("", 7) == 7);
    CHECK(presets::EntityPaletteNames.size() == presets::EntityPaletteRgb.size());
    for (const auto& c : presets::EntityPaletteRgb) CHECK(c.r >= 0 && c.r <= 1 && c.g >= 0 && c.g <= 1 && c.b >= 0 && c.b <= 1);

    // ---- keys: unique per category ----
    {
        std::set<std::string> keys;
        for (int k = 0; k < kEntityKindCount; ++k)
        {
            CHECK(keys.insert(EntityKeysFor(k).show).second && keys.insert(EntityKeysFor(k).color).second);
        }
        CHECK(std::string(EntityKeysFor(-3).show) == EntityKeysFor(0).show && std::string(EntityKeysFor(99).color) == EntityKeysFor(0).color);
    }

    // ---- hotkeys: every action has its own distinct text slot and its own key name ----
    {
        using namespace hotkeys;
        EspConfig c;
        std::set<const std::string*> slots;
        std::set<std::string> names;
        for (int a = 0; a < kActionCount; ++a)
        {
            const Action act = static_cast<Action>(a);
            CHECK(slots.insert(&c.HotkeyText(act)).second); // no two actions share a stored text
            CHECK(names.insert(Def(act).key).second);
        }
        for (Action a : {Action::ToggleEntities, Action::ToggleHostile, Action::TogglePassive, Action::ToggleNeutral, Action::ToggleProjectiles, Action::ToggleXp})
            CHECK(!Def(a).required && !Defaults()[static_cast<size_t>(a)].Bound()); // optional, and not bound until the user picks a key
        // a binding set through the file reaches the right action
        const auto back = configschema::LoadText(R"({"HotkeyToggleProjectiles":"F7","HotkeyToggleXp":"F8"})");
        CHECK(back.Warnings() == 0 && back.cfg.HotkeyToggleProjectiles == "F7" && back.cfg.HotkeyToggleXp == "F8" && back.cfg.HotkeyToggleHostile.empty());
    }

    if (fails) std::printf("%d failure(s)\n", fails);
    else std::printf("entity_settings_test: ok\n");
    return fails ? 1 : 0;
}
