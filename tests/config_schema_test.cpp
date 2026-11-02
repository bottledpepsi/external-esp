// config_schema_test - the settings file contract: every key has a spec and a default, validation says what it replaced, unknown keys survive,
// and old files are migrated step by step.
#include "ConfigSchema.h"
#include "EspConfig.h"
#include "Json.h"
#include "Paths.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
namespace fs = std::filesystem;
using namespace configschema;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

static bool HasWarning(const LoadResult& r, const std::string& key, const std::string& part)
{
    for (const auto& i : r.issues)
        if (i.level == Issue::Level::Warning && i.key == key && i.message.find(part) != std::string::npos) return true;
    return false;
}

int main()
{
    // ---- the table and EspConfig agree: every key written by ToJson has a spec, and every spec is written ----
    {
        const auto o = json::ParseObject(EspConfig{}.ToJson());
        CHECK(o.has_value());
        std::vector<std::string> written;
        for (const auto& kv : *o) if (kv.first != "Version") written.push_back(kv.first);
        std::vector<std::string> known = KnownKeys();
        std::sort(written.begin(), written.end());
        std::sort(known.begin(), known.end());
        CHECK(written == known); // adding a key to EspConfig without a spec (or the reverse) fails here
        CHECK(json::GetNumber(*o, "Version", -1) == kCurrentVersion);
        // the written defaults pass their own validation untouched
        const LoadResult r = Load(*o);
        CHECK(r.Warnings() == 0 && !r.migrated && r.fileVersion == kCurrentVersion);
        CHECK(r.cfg.ToJson() == EspConfig{}.ToJson());
    }

    // ---- an empty object / a file from the C# tool (version 0) ----
    {
        const LoadResult r = LoadText("{}");
        CHECK(r.fileVersion == 0 && r.migrated && r.Warnings() == 0);
        CHECK(r.cfg.ToJson() == EspConfig{}.ToJson());
        CHECK(r.Summary() == "version 0 -> 1, 0 warning(s)");
        const LoadResult c = LoadText(R"({"HotkeyToggle":"F6","BoxStyle":"Corner-only","CornerRadius":6.0,"FpsCap":120})");
        CHECK(c.Warnings() == 0 && c.cfg.HotkeyToggle == "F6" && c.cfg.BoxStyle == "Corner-only" && c.cfg.FpsCap == 120);
    }

    // ---- validation: type, range, words, key names ----
    {
        const LoadResult r = LoadText(R"({"BoxThickness":"thick","NameSize":99,"MinimapSize":-5,"BoxStyle":"Fancy","ShowPlayers":1,
                                          "HotkeyConfig":"Banana","TracerTarget":7,"Fov":50,"MaxLabelDist":-1,"FpsCap":0})");
        CHECK(r.Warnings() == 8);
        CHECK(HasWarning(r, "BoxThickness", "expected a number, found text - using the default"));
        CHECK(HasWarning(r, "NameSize", "99 is outside 10..17 - using 17") && r.cfg.NameSize == 17);
        CHECK(HasWarning(r, "MinimapSize", "-5 is outside 120..220 - using 120") && r.cfg.MinimapSize == 120);
        CHECK(HasWarning(r, "BoxStyle", "'Fancy' is not one of Rounded, Sharp, Corner-only, Skeleton - using Sharp") && r.cfg.BoxStyle == "Sharp");
        CHECK(HasWarning(r, "ShowPlayers", "expected true or false, found a number") && r.cfg.ShowPlayers);
        CHECK(HasWarning(r, "HotkeyConfig", "'Banana' is not a key name") && r.cfg.HotkeyConfig == "Insert");
        CHECK(HasWarning(r, "TracerTarget", "is not one of Feet, Center, Head") && r.cfg.TracerTarget == "Center");
        CHECK(HasWarning(r, "Fov", "50 is outside 60..110") == true && r.cfg.Fov == 60);
        CHECK(r.cfg.BoxThickness == 1);
        CHECK(r.cfg.MaxLabelDist == -1 && r.cfg.FpsCap == 0); // the "unlimited" markers are valid
    }

    // ---- numbers that are not numbers: no UB, a warning, the default ----
    {
        const LoadResult r = LoadText(R"({"NameSize":-inf,"BoxThickness":1e999,"Fov":-nan})");
        CHECK(r.Warnings() == 3);
        CHECK(r.cfg.NameSize == EspConfig{}.NameSize && r.cfg.BoxThickness == EspConfig{}.BoxThickness && r.cfg.Fov == EspConfig{}.Fov);
    }

    // ---- unknown keys are kept, through a whole load -> Settings -> save -> load cycle ----
    {
        const char* text = R"({"Version":1,"BoxStyle":"Skeleton","Note":"mine","FutureNumber":3.5,"FutureFlag":true,
                               "Nested":{"a":[1,2,{"b":"c}"}],"d":null},"List":[1,"x"]})";
        const LoadResult r = LoadText(text);
        CHECK(r.Warnings() == 0 && r.cfg.BoxStyle == "Skeleton");
        CHECK(r.cfg.Unknown.size() == 5);
        CHECK(r.cfg.Unknown.at("Note") == "\"mine\"" && r.cfg.Unknown.at("FutureNumber") == "3.5" && r.cfg.Unknown.at("FutureFlag") == "true");
        Settings s;
        s.Apply(r.cfg);
        const std::string out = s.Build().ToJson();
        const LoadResult again = LoadText(out);
        CHECK(again.Warnings() == 0 && again.cfg.Unknown == r.cfg.Unknown); // nothing lost, nothing changed
        CHECK(json::IsValid(out));
        CHECK(again.cfg.Unknown.at("Nested") == r.cfg.Unknown.at("Nested"));
        // a known key is never also written as an unknown one
        CHECK(again.cfg.Unknown.count("BoxStyle") == 0 && again.cfg.Unknown.count("Version") == 0);
    }

    // ---- an unknown key whose text would break the file is dropped, with a warning ----
    {
        const LoadResult r = LoadText(R"({"Good":[1,2],"Bad":{oops}})");
        CHECK(r.cfg.Unknown.count("Good") == 1 && r.cfg.Unknown.count("Bad") == 0);
        CHECK(HasWarning(r, "Bad", "not valid JSON - dropped"));
        CHECK(json::IsValid(r.cfg.ToJson()));
    }

    // ---- a file from a newer build ----
    {
        const LoadResult r = LoadText(R"({"Version":7,"BoxStyle":"Rounded","Shiny":{"x":1}})");
        CHECK(r.fileVersion == 7 && !r.migrated && r.Warnings() == 1);
        CHECK(HasWarning(r, "Version", "newer build (version 7"));
        CHECK(r.cfg.BoxStyle == "Rounded" && r.cfg.Unknown.count("Shiny") == 1);
        CHECK(LoadText(R"({"Version":"two"})").Warnings() == 1);
        CHECK(LoadText(R"({"Version":1.5})").Warnings() == 1);
    }

    // ---- not JSON at all ----
    {
        bool parsed = true;
        const LoadResult r = LoadText("this is not json", &parsed);
        CHECK(!parsed && r.Warnings() == 1 && r.cfg.ToJson() == EspConfig{}.ToJson());
        parsed = true;
        LoadText("[1,2,3]", &parsed);
        CHECK(!parsed);
    }

    // ---- migration chain with steps of our own (the production table has a single v0 -> v1 step) ----
    {
        std::vector<StepEntry> steps;
        steps.push_back({1, [](json::Object& o, std::vector<Issue>&)
        {
            auto it = o.find("OldName"); // v1 -> v2: a rename
            if (it != o.end()) { o["NewName"] = it->second; o.erase(it); }
        }});
        steps.push_back({2, [](json::Object& o, std::vector<Issue>& issues)
        {
            auto it = o.find("NewName"); // v2 -> v3: a value is recomputed
            if (it != o.end()) { it->second.n *= 10; issues.push_back({Issue::Level::Info, "NewName", "scaled"}); }
        }});
        json::Object o;
        o["OldName"].type = json::Value::Type::Number;
        o["OldName"].n = 4;
        std::vector<Issue> issues;
        Migrate(o, 1, 3, steps, issues);
        CHECK(o.count("OldName") == 0 && o.at("NewName").n == 40 && issues.size() == 1);
        // already current: nothing runs
        json::Object p = o;
        issues.clear();
        Migrate(p, 3, 3, steps, issues);
        CHECK(issues.empty() && p.at("NewName").n == 40);
        // a missing step is reported, not skipped silently
        json::Object q;
        Migrate(q, 3, 4, steps, issues);
        CHECK(issues.size() == 1 && issues[0].level == Issue::Level::Warning);
        // the real table starts at 0 and has no gaps up to the current version
        for (int v = 0; v < kCurrentVersion; ++v)
            CHECK(std::any_of(Steps().begin(), Steps().end(), [v](const StepEntry& e) { return e.from == v; }));
    }

    // ---- Settings::Load reads the notes ----
    {
        const fs::path dir = fs::current_path() / "schematest";
        fs::remove_all(dir);
        fs::create_directories(dir);
        paths::ConfigDirOverride() = dir;
        std::ofstream(dir / "esp_config.json") << R"({"Version":1,"NameSize":50,"BoxStyle":"Skeleton","Keep":{"a":1}})";
        Settings s;
        s.Load();
        CHECK(s.loadNotes.size() == 1 && s.loadNotes[0].find("NameSize") == 0);
        CHECK(s.boxStyleIdx == 3 && s.nameSizeIdx == 2);
        CHECK(s.unknownKeys.count("Keep") == 1);
        s.Save();
        Settings t;
        t.Load();
        CHECK(t.loadNotes.empty() && t.unknownKeys.count("Keep") == 1 && t.boxStyleIdx == 3);
        paths::ConfigDirOverride().clear();
        fs::remove_all(dir);
    }

    // ---- the strict validator, and raw text capture ----
    {
        CHECK(json::IsValid("{}") && json::IsValid("[1, 2.5e3, -0.1, \"a\\u00e9\\n\", true, false, null, {\"k\": []}]"));
        CHECK(json::IsValid("  42  ") && json::IsValid("\"s\""));
        CHECK(!json::IsValid("") && !json::IsValid("{") && !json::IsValid("{\"a\":}") && !json::IsValid("[1,]") && !json::IsValid("{\"a\":1,}"));
        CHECK(!json::IsValid("01") && !json::IsValid("1.") && !json::IsValid(".5") && !json::IsValid("+1") && !json::IsValid("-"));
        CHECK(!json::IsValid("\"a\nb\"") && !json::IsValid("\"\\x\"") && !json::IsValid("\"\\u12\"") && !json::IsValid("tru") && !json::IsValid("1 2"));
        CHECK(!json::IsValid("-inf") && !json::IsValid("nan") && !json::IsValid("0x10"));
        std::string deep(70, '[');
        deep += std::string(70, ']');
        CHECK(!json::IsValid(deep)); // depth limit
        const auto o = json::ParseObject(R"({"a": "x y", "b" : [1, {"c":2}] ,"d":  -1.50 })");
        CHECK(o && o->at("a").raw == "\"x y\"" && o->at("b").raw == "[1, {\"c\":2}]" && o->at("d").raw == "-1.50");
    }

    if (fails) std::printf("%d failure(s)\n", fails);
    else std::puts("config_schema_test: all passed");
    return fails ? 1 : 0;
}
