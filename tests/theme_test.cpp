// theme_test - Theme.h: hex colors, parsing with every kind of bad value, built-in themes, applying and re-reading a look, the themes folder, the
// settings file keys, and that the renderer really draws with the theme's colors.
#include "ConfigSchema.h"
#include "Theme.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
static bool Near(float a, float b, float e = 1.5f / 255.f) { return std::fabs(a - b) <= e; }
using namespace theme;

int main()
{
    // ---- hex ----
    {
        Rgba c;
        CHECK(ParseHex("#FF8000", c) && Near(c.r, 1) && Near(c.g, 128 / 255.f) && Near(c.b, 0) && c.a == 1.f);
        CHECK(ParseHex("#ff800080", c) && Near(c.a, 128 / 255.f));
        const char* bad[] = {"", "#", "FF8000", "#FF80", "#FF80000", "#FF800000FF", "#GG0000", "#FF 000", "#-F0000", "0xFF8000", " #FF8000", "#FF8000 ", "#ＦＦ8000"};
        for (const char* b : bad) CHECK(!ParseHex(b, c));
        CHECK(ToHex({1, 0.5f, 0, 1}) == "#FF8000" && ToHex({1, 0.5f, 0, 0.5f}) == "#FF800080" && ToHex({2, -1, 0, 7}) == "#FF0000"); // clamped
        for (int v = 0; v < 256; v += 5)
        {
            Rgba a{v / 255.f, (255 - v) / 255.f, (v * 7 % 256) / 255.f, (v * 3 % 256) / 255.f}, b;
            CHECK(ParseHex(ToHex(a), b) && Near(a.r, b.r, 0.5f / 255.f + 1e-6f) && Near(a.g, b.g, 0.5f / 255.f + 1e-6f) && Near(a.a, b.a, 0.5f / 255.f + 1e-6f));
        }
        // the defaults' own text is what the config table says
        for (int i = 0; i < kColorKeyCount; ++i)
        {
            Rgba p;
            CHECK(ParseHex(ColorKeyAt(i).defaultHex, p) && ToHex(Colors{}.*ColorKeyAt(i).member) == ColorKeyAt(i).defaultHex);
        }
    }

    // ---- parsing ----
    {
        const auto r = Parse(R"({"name":"Mine","enemy":"#112233","friend":"#445566AA","text":"#FFFFFF","distance":"#000000","item":"#010203","itemEnchanted":"#040506","death":"#070809",
                                 "boxStyle":"Skeleton","lineWidth":2.5,"opacity":0.5,"font":"Consolas"})");
        CHECK(r.ok && r.warnings.empty() && r.theme.name == "Mine" && r.theme.boxStyle == 3 && r.theme.lineWidth == 2.5f && r.theme.opacity == 0.5f && r.theme.font == "Consolas");
        CHECK(Near(r.theme.c.enemy.r, 0x11 / 255.f) && Near(r.theme.c.friendly.a, 0xAA / 255.f) && Near(r.theme.c.death.b, 9 / 255.f));
        // an empty object: all defaults, nothing overridden
        const auto e = Parse("{}");
        CHECK(e.ok && e.warnings.empty() && e.theme.boxStyle == -1 && e.theme.lineWidth < 0 && e.theme.opacity < 0 && e.theme.font.empty() && e.theme.name.empty());
        // every kind of bad value is reported, ignored, and does not stop the others
        const auto b = Parse(R"({"enemy":"red","friend":5,"text":"#12","boxStyle":"Round","lineWidth":99,"opacity":-1,"font":"a\u0007b","name":"x","unknownKey":1,"item":"#ABCDEF"})");
        CHECK(b.ok && b.warnings.size() == 8); // enemy, friend, text, boxStyle, lineWidth, opacity, font, unknown key
        CHECK(b.theme.c.enemy.r == Colors{}.enemy.r && b.theme.boxStyle == -1 && b.theme.lineWidth < 0 && b.theme.opacity < 0 && b.theme.font.empty() && b.theme.name == "x");
        CHECK(Near(b.theme.c.item.r, 0xAB / 255.f)); // the good one went through
        CHECK(!Parse("").ok && !Parse("not json").ok && !Parse("[1,2]").ok && !Parse("{\"a\":").ok && !Parse("42").ok);
        CHECK(!Parse(std::string(kMaxFileBytes + 1, ' ')).ok);
        CHECK(Parse(R"({"lineWidth":"3"})").warnings.size() == 1 && Parse(R"({"name":")" + std::string(41, 'a') + R"("})").warnings.size() == 1);
        CHECK(Parse(R"({"font":")" + std::string(65, 'f') + R"("})").theme.font.empty());
        // numbers on the boundary
        CHECK(Parse(R"({"lineWidth":0.5,"opacity":0})").warnings.empty() && Parse(R"({"lineWidth":8,"opacity":1})").warnings.empty());
        CHECK(Parse(R"({"lineWidth":0.49})").warnings.size() == 1 && Parse(R"({"opacity":1.01})").warnings.size() == 1);
    }

    // ---- write and read back ----
    for (const Theme& t : BuiltIn())
    {
        const auto r = Parse(ToJson(t));
        CHECK(r.ok && r.warnings.empty() && r.theme.name == t.name && r.theme.boxStyle == t.boxStyle && r.theme.font == t.font);
        CHECK(Near(r.theme.lineWidth, t.lineWidth, 1e-4f) || (t.lineWidth <= 0 && r.theme.lineWidth < 0));
        CHECK(ToHex(r.theme.c.enemy) == ToHex(t.c.enemy) && ToHex(r.theme.c.distance) == ToHex(t.c.distance) && ToHex(r.theme.c.death) == ToHex(t.c.death));
    }
    CHECK(BuiltIn().size() == 4 && BuiltIn()[0].name == "default" && FindBuiltIn("high-contrast") && !FindBuiltIn("nope"));
    {
        // the default theme IS the look from before themes: applying it to fresh Settings changes nothing
        Settings a, b;
        Apply(*FindBuiltIn("default"), b);
        CHECK(ToHex(a.colors.enemy) == ToHex(b.colors.enemy) && a.boxStyleIdx == b.boxStyleIdx && a.boxThickIdx == b.boxThickIdx && a.itemOpacityIdx == b.itemOpacityIdx &&
              b.fontFamily.empty());
        CHECK(a.Build().ToJson() == b.Build().ToJson());
    }

    // ---- apply / from settings ----
    {
        Settings s;
        Apply(*FindBuiltIn("high-contrast"), s);
        CHECK(ToHex(s.colors.enemy) == "#FFFF00" && s.boxStyleIdx == 1 && presets::Thickness[static_cast<size_t>(s.boxThickIdx)] == presets::Thickness[static_cast<size_t>(Nearest(presets::Thickness, 3))]);
        CHECK(s.boxThickIdx == s.tracerThickIdx && presets::TraceOpacities[static_cast<size_t>(s.itemOpacityIdx)] >= presets::TraceOpacities.back() - 1e-6f);
        // a theme without overrides leaves the rest of the settings alone
        s.boxStyleIdx = 2;
        s.boxThickIdx = 0;
        Theme only;
        only.c.enemy = Hex("#123456");
        Apply(only, s);
        CHECK(ToHex(s.colors.enemy) == "#123456" && s.boxStyleIdx == 2 && s.boxThickIdx == 0);
        // what is on the settings can become a theme and back
        s.fontFamily = "Consolas";
        const Theme t = FromSettings(s, "round trip name that is much longer than forty characters indeed");
        CHECK(t.name.size() == kMaxNameLen && t.boxStyle == 2 && t.font == "Consolas" && ToHex(t.c.enemy) == "#123456");
        Settings s2;
        Apply(Parse(ToJson(t)).theme, s2);
        CHECK(ToHex(s2.colors.enemy) == "#123456" && s2.boxStyleIdx == 2 && s2.fontFamily == "Consolas" && s2.boxThickIdx == s.boxThickIdx && s2.itemOpacityIdx == s.itemOpacityIdx);
        CHECK(Nearest({1.f, 2.f, 4.f}, 3.4f) == 2 && Nearest({1.f, 2.f, 4.f}, -5) == 0 && Nearest({}, 1) == 0);
    }

    // ---- the settings file ----
    {
        Settings s;
        Apply(*FindBuiltIn("colorblind"), s);
        s.fontFamily = "Segoe UI";
        const auto back = configschema::LoadText(s.Build().ToJson());
        Settings t;
        t.Apply(back.cfg);
        CHECK(back.Warnings() == 0 && ToHex(t.colors.enemy) == "#FF8C00" && ToHex(t.colors.friendly) == "#1E90FF" && t.fontFamily == "Segoe UI");
        CHECK(back.cfg.ThemeColor[0] == "#FF8C00");
        // an untouched Settings writes the default texts and reads back the exact default floats
        Settings d;
        const auto dback = configschema::LoadText(d.Build().ToJson());
        Settings e;
        e.Apply(dback.cfg);
        CHECK(dback.Warnings() == 0 && e.colors.enemy.g == Colors{}.enemy.g && e.colors.distance.a == Colors{}.distance.a && dback.cfg.ThemeColor[3] == "#D9E0F2E6");
        const auto bad = configschema::LoadText(R"({"ColorEnemy":"red","ColorText":7,"FontFamily":3})");
        CHECK(bad.Warnings() == 3 && bad.cfg.ThemeColor[0] == "#FF2929" && bad.cfg.FontFamily.empty());
        EspConfig c;
        c.ThemeColor[0] = "garbage"; // slipped past the schema: Apply falls back to the default color
        c.FontFamily = "bad\x07font";
        Settings f;
        f.Apply(c);
        CHECK(f.colors.enemy.r == Colors{}.enemy.r && f.fontFamily.empty());
        CHECK(configschema::LoadText(std::string(R"({"FontFamily":")") + std::string(200, 'x') + "\"}").Warnings() == 1); // cut off with a warning
    }

    // ---- names ----
    CHECK(SanitizeName("My Theme 2") == "My Theme 2" && SanitizeName("a/b\\c:d*e?") == "a_b_c_d_e_" && SanitizeName("") == "theme" && SanitizeName("...") == "theme");
    CHECK(SanitizeName("  .x. ") == "x" && SanitizeName("CON") == "_CON" && SanitizeName("nul.txt") == "_nul.txt" && SanitizeName("com1") == "_com1" && SanitizeName("console") == "console");
    CHECK(SanitizeName(std::string(100, 'a')).size() == kMaxNameLen);
    for (const char* evil : {"../../x", "..\\..\\x", "a/../b", "C:\\x", "/etc/passwd", "x\0y"})
    {
        const std::string n = SanitizeName(evil);
        CHECK(n.find('/') == std::string::npos && n.find('\\') == std::string::npos && n.find(':') == std::string::npos && n.find('\0') == std::string::npos && n[0] != '.');
    }

    // ---- the folder ----
    {
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / "esp_theme_test";
        std::filesystem::remove_all(dir);
        CHECK(List(dir).empty() && !Load(dir, "nothing").ok); // no folder yet
        Theme t = *FindBuiltIn("soft");
        t.name = "My/Soft: one";
        std::string err, as;
        CHECK(Save(dir, t, &err, &as) && as == "My_Soft_ one" && err.empty());
        Theme u = *FindBuiltIn("colorblind");
        u.name = "alpha";
        CHECK(Save(dir, u));
        CHECK(Save(dir, u)); // saving again replaces it
        Theme v;
        v.name = "";
        CHECK(Save(dir, v, nullptr, &as) && as == "theme");
        const auto names = List(dir);
        CHECK(names.size() == 3 && names[0] == "alpha" && names[1] == "My_Soft_ one" && names[2] == "theme");
        const auto r = Load(dir, "alpha");
        CHECK(r.ok && r.warnings.empty() && r.theme.name == "alpha" && ToHex(r.theme.c.enemy) == "#FF8C00");
        CHECK(Load(dir, "My/Soft: one").ok); // the same name sanitizes to the same file
        // a file that is not a theme, an oversized one, and a stray non-json file
        { std::ofstream(dir / "broken.json") << "{ this is not json"; }
        { std::ofstream(dir / "huge.json") << std::string(kMaxFileBytes + 100, ' '); }
        { std::ofstream(dir / "notes.txt") << "hello"; }
        CHECK(List(dir).size() == 5 && !Load(dir, "broken").ok && !Load(dir, "huge").ok && !Load(dir, "huge").warnings.empty());
        CHECK(!Load(dir, "../alpha").ok); // path tricks never leave the folder: "../alpha" is the file "_.._alpha.json" in it, which is not there
        CHECK(Delete(dir, "alpha") && !Delete(dir, "alpha") && !Load(dir, "alpha").ok && List(dir).size() == 4);
        std::filesystem::remove_all(dir);
    }

    if (fails) std::printf("%d failure(s)\n", fails);
    else std::printf("theme_test: ok\n");
    return fails ? 1 : 0;
}
