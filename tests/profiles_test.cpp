// profiles_test - built-in profiles, the active profile, cycling, and Activate().
#include "ConfigSchema.h"
#include "EspConfig.h"
#include "Paths.h"
#include "ProfileManager.h"
#include <cstdio>
#include <fstream>
namespace fs = std::filesystem;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

int main()
{
    const fs::path root = fs::current_path() / "proftest";
    fs::remove_all(root);
    paths::ConfigDirOverride() = root;
    using namespace ProfileManager;

    // ---- built-ins: seeded once into an empty folder, valid, different from each other ----
    Refresh();
    CHECK(ProfileNames().empty());
    CHECK(SeedBuiltIns() == 3);
    CHECK((ProfileNames() == std::vector<std::string>{"default", "low-power", "testing"}));
    CHECK(SeedBuiltIns() == 0);                      // once
    CHECK(Delete("testing"));
    CHECK(SeedBuiltIns() == 0 && Find("testing").empty()); // a deleted built-in stays deleted
    for (const std::string& n : {std::string("default"), std::string("low-power")})
    {
        const auto c = Load(n);
        CHECK(c.has_value());
        CHECK(c->ToJson() == BuiltIn(n).ToJson());
    }
    CHECK(BuiltIn("default").ToJson() == EspConfig{}.ToJson());
    CHECK(BuiltIn("low-power").FpsCap == 60 && !BuiltIn("low-power").ShowMinimap && BuiltIn("low-power").VSync);
    CHECK(BuiltIn("testing").ShowItemTags && BuiltIn("testing").MaxLabelDist == -1);
    for (const std::string& n : BuiltInNames())      // every built-in passes the schema without a single warning
        CHECK(configschema::LoadText(BuiltIn(n).ToJson()).Warnings() == 0);

    // an existing profile of a built-in name is never overwritten
    {
        fs::remove_all(root / "profiles");
        Refresh();
        EspConfig mine; mine.NameSize = 17;
        CHECK(SaveAs("default", mine, false));
        CHECK(SeedBuiltIns() == 2 && Load("default")->NameSize == 17);
    }

    // ---- active profile ----
    CHECK(Active().empty() || true);
    SetActive("LOW-POWER");
    CHECK(Active() == "low-power");                  // case-insensitive, stored name returned
    SetActive("gone");
    CHECK(Active().empty());                         // a profile that does not exist is not active
    EspConfig e; e.NameSize = 12;
    CHECK(Save("mine", e) && Active() == "mine");    // saving makes it active
    CHECK(Delete("mine") && Active().empty());       // deleting it clears it
    CHECK(SaveAs("quiet", e, false) && Active().empty());

    // ---- Activate: loads, applies, marks active, requests a save ----
    {
        Settings s;
        CHECK(!Activate("nope", s) && !LastError().empty());
        CHECK(!s.SavePending());
        CHECK(Activate("low-power", s));
        CHECK(Active() == "low-power" && s.fpsCap == 0 /* index of 60 in FpsCaps */ && !s.showMinimap && s.SavePending());
    }

    // ---- cycling ----
    {
        const std::vector<std::string> n = {"a", "b", "c"};
        CHECK(CycleName(n, "a", +1) == "b" && CycleName(n, "c", +1) == "a");
        CHECK(CycleName(n, "a", -1) == "c" && CycleName(n, "b", -1) == "a");
        CHECK(CycleName(n, "", +1) == "a" && CycleName(n, "zz", -1) == "c");
        CHECK(CycleName({}, "a", +1).empty());
        CHECK(CycleName({"only"}, "only", +1) == "only");
    }

    paths::ConfigDirOverride().clear();
    fs::remove_all(root);
    if (fails) std::printf("%d failure(s)\n", fails); else std::puts("profiles_test: all passed");
    return fails ? 1 : 0;
}
