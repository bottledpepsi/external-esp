// Theme.h - the overlay's look as data: the colors of the things it draws, a box style, a line width, an opacity and a font; built-in themes; a theme
// as a small JSON file that can be exported, shared and imported; and a folder of them (themes\*.json in the settings folder).
//
// A theme is applied to the Settings (Apply) - the colors are kept in the settings file like every other setting, so profiles carry them - and the
// current look can be turned back into a theme (FromSettings). The renderer reads the colors from the Settings, never from a theme file.
//
//   file:  { "name": "My theme", "enemy": "#FF2929", "friend": "#29FF59", "text": "#FFFFFF", "distance": "#D9E0F2E6",
//            "item": "#FFFFFF", "itemEnchanted": "#9EC7FF", "death": "#FF4040",
//            "boxStyle": "Sharp", "lineWidth": 2, "opacity": 0.75, "font": "Segoe UI" }
//   Colors are #RRGGBB or #RRGGBBAA. Every key is optional: a missing one leaves the default (colors) or the current setting (the rest).
//   A bad value is reported in the warnings and ignored; unknown keys are ignored. The file is read as data only.
//
// Portable (no Windows headers); the folder functions take the directory so the tests can use a temporary one.
#pragma once
#include "ConfigStore.h"
#include "EspConfig.h"
#include "Json.h"
#include "Presets.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace theme
{
    inline constexpr size_t kMaxFileBytes = 16 * 1024;
    inline constexpr size_t kMaxNameLen = 40;
    inline constexpr size_t kMaxFontLen = 64;

    inline int HexDigit(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    // "#RRGGBB" or "#RRGGBBAA" (exactly, nothing else).
    inline bool ParseHex(std::string_view s, Rgba& out)
    {
        if ((s.size() != 7 && s.size() != 9) || s[0] != '#') return false;
        int v[8] = {};
        for (size_t i = 1; i < s.size(); ++i)
            if ((v[i - 1] = HexDigit(s[i])) < 0) return false;
        out.r = static_cast<float>(v[0] * 16 + v[1]) / 255.f;
        out.g = static_cast<float>(v[2] * 16 + v[3]) / 255.f;
        out.b = static_cast<float>(v[4] * 16 + v[5]) / 255.f;
        out.a = s.size() == 9 ? static_cast<float>(v[6] * 16 + v[7]) / 255.f : 1.f;
        return true;
    }
    inline std::string ToHex(const Rgba& c)
    {
        auto b = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.f, 1.f) * 255.f)); };
        char buf[16];
        if (b(c.a) == 255) std::snprintf(buf, sizeof buf, "#%02X%02X%02X", b(c.r), b(c.g), b(c.b));
        else std::snprintf(buf, sizeof buf, "#%02X%02X%02X%02X", b(c.r), b(c.g), b(c.b), b(c.a));
        return buf;
    }

    struct Theme
    {
        std::string name;
        Colors c;
        int boxStyle = -1; // BoxStyle index, -1 = leave the setting
        float lineWidth = -1; // px, -1 = leave
        float opacity = -1; // 0..1, -1 = leave
        std::string font; // preferred font family, "" = leave
    };

    inline const char* BoxStyleName(int i) { return i == 0 ? "Rounded" : i == 1 ? "Sharp" : i == 2 ? "CornerOnly" : i == 3 ? "Skeleton" : ""; }
    inline int BoxStyleFromName(const std::string& n)
    {
        for (int i = 0; i < 4; ++i)
            if (n == BoxStyleName(i)) return i;
        return -1;
    }

    inline bool GoodText(const std::string& s, size_t maxLen)
    {
        if (s.size() > maxLen) return false;
        for (unsigned char ch : s)
            if (ch < 0x20 || ch == 0x7F) return false;
        return true;
    }

    struct ParseResult
    {
        bool ok = false; // the text was a JSON object
        Theme theme;
        std::vector<std::string> warnings; // one line each: what was wrong and what was done about it
    };

    inline ParseResult Parse(const std::string& text)
    {
        ParseResult r;
        if (text.size() > kMaxFileBytes)
        {
            r.warnings.push_back("the theme is larger than " + std::to_string(kMaxFileBytes / 1024) + " KB - not read");
            return r;
        }
        std::string err;
        const auto o = json::ParseObject(text, &err);
        if (!o || !json::IsValid(text))
        {
            r.warnings.push_back("not a JSON object" + (err.empty() ? std::string() : ": " + err));
            return r;
        }
        r.ok = true;
        auto warn = [&](const std::string& s) { r.warnings.push_back(s); };
        auto color = [&](const char* key, Rgba& dst)
        {
            const auto it = o->find(key);
            if (it == o->end()) return;
            Rgba c;
            if (it->second.type != json::Value::Type::String || !ParseHex(it->second.s, c)) warn(std::string(key) + ": expected #RRGGBB or #RRGGBBAA - keeping the default");
            else dst = c;
        };
        color("enemy", r.theme.c.enemy);
        color("friend", r.theme.c.friendly);
        color("text", r.theme.c.text);
        color("distance", r.theme.c.distance);
        color("item", r.theme.c.item);
        color("itemEnchanted", r.theme.c.itemEnchanted);
        color("death", r.theme.c.death);
        if (auto it = o->find("name"); it != o->end())
        {
            if (it->second.type != json::Value::Type::String || !GoodText(it->second.s, kMaxNameLen)) warn("name: expected text of at most 40 characters - left empty");
            else r.theme.name = it->second.s;
        }
        if (auto it = o->find("boxStyle"); it != o->end())
        {
            const int b = it->second.type == json::Value::Type::String ? BoxStyleFromName(it->second.s) : -1;
            if (b < 0) warn("boxStyle: expected Rounded, Sharp, CornerOnly or Skeleton - leaving the box style alone");
            else r.theme.boxStyle = b;
        }
        if (auto it = o->find("lineWidth"); it != o->end())
        {
            if (it->second.type != json::Value::Type::Number || !(it->second.n >= 0.5 && it->second.n <= 8.0)) warn("lineWidth: expected a number from 0.5 to 8 - leaving it alone");
            else r.theme.lineWidth = static_cast<float>(it->second.n);
        }
        if (auto it = o->find("opacity"); it != o->end())
        {
            if (it->second.type != json::Value::Type::Number || !(it->second.n >= 0.0 && it->second.n <= 1.0)) warn("opacity: expected a number from 0 to 1 - leaving it alone");
            else r.theme.opacity = static_cast<float>(it->second.n);
        }
        if (auto it = o->find("font"); it != o->end())
        {
            if (it->second.type != json::Value::Type::String || !GoodText(it->second.s, kMaxFontLen)) warn("font: expected a font family name of at most 64 characters - leaving it alone");
            else r.theme.font = it->second.s;
        }
        static const char* const kKnown[] = {"name", "enemy", "friend", "text", "distance", "item", "itemEnchanted", "death", "boxStyle", "lineWidth", "opacity", "font"};
        for (const auto& kv : *o)
            if (std::find_if(std::begin(kKnown), std::end(kKnown), [&](const char* k) { return kv.first == k; }) == std::end(kKnown))
                warn("unknown key \"" + kv.first.substr(0, 40) + "\" - ignored");
        return r;
    }

    inline std::string ToJson(const Theme& t)
    {
        std::string j = "{\n";
        auto kv = [&](const char* k, const std::string& raw) { j += std::string("  \"") + k + "\": " + raw + ",\n"; };
        kv("name", json::Escape(t.name));
        kv("enemy", json::Escape(ToHex(t.c.enemy)));
        kv("friend", json::Escape(ToHex(t.c.friendly)));
        kv("text", json::Escape(ToHex(t.c.text)));
        kv("distance", json::Escape(ToHex(t.c.distance)));
        kv("item", json::Escape(ToHex(t.c.item)));
        kv("itemEnchanted", json::Escape(ToHex(t.c.itemEnchanted)));
        kv("death", json::Escape(ToHex(t.c.death)));
        if (t.boxStyle >= 0 && t.boxStyle < 4) kv("boxStyle", json::Escape(BoxStyleName(t.boxStyle)));
        if (t.lineWidth > 0) kv("lineWidth", json::Number(t.lineWidth));
        if (t.opacity >= 0) kv("opacity", json::Number(t.opacity));
        if (!t.font.empty()) kv("font", json::Escape(t.font));
        j.erase(j.size() - 2, 1); // the last comma
        return j + "}\n";
    }

    inline int Nearest(const std::vector<float>& v, float x)
    {
        int best = 0;
        for (size_t i = 1; i < v.size(); ++i)
            if (std::fabs(v[i] - x) < std::fabs(v[static_cast<size_t>(best)] - x)) best = static_cast<int>(i);
        return best;
    }

    // Puts the theme on the settings: colors always; the box style, line width (box and tracer thickness, the nearest preset), opacity (item and tracer
    // opacity, the nearest preset) and font only when the theme has them.
    inline void Apply(const Theme& t, Settings& s)
    {
        s.colors = t.c;
        if (t.boxStyle >= 0 && t.boxStyle < 4) s.boxStyleIdx = t.boxStyle;
        if (t.lineWidth > 0) s.boxThickIdx = s.tracerThickIdx = Nearest(presets::Thickness, t.lineWidth);
        if (t.opacity >= 0) s.itemOpacityIdx = s.tracerOpacityIdx = Nearest(presets::TraceOpacities, t.opacity);
        if (!t.font.empty()) s.fontFamily = t.font;
    }

    inline Theme FromSettings(const Settings& s, const std::string& name)
    {
        Theme t;
        t.name = name.substr(0, kMaxNameLen);
        t.c = s.colors;
        t.boxStyle = std::clamp(s.boxStyleIdx, 0, 3);
        t.lineWidth = presets::Thickness[static_cast<size_t>(std::clamp(s.boxThickIdx, 0, static_cast<int>(presets::Thickness.size()) - 1))];
        t.opacity = presets::TraceOpacities[static_cast<size_t>(std::clamp(s.itemOpacityIdx, 0, static_cast<int>(presets::TraceOpacities.size()) - 1))];
        t.font = s.fontFamily;
        return t;
    }

    inline Rgba Hex(const char* h)
    {
        Rgba c;
        ParseHex(h, c);
        return c;
    }

    // "default" (what the overlay looked like before themes: the colors, nothing else touched), "high-contrast", "colorblind", "soft".
    inline const std::vector<Theme>& BuiltIn()
    {
        static const std::vector<Theme> k = [] {
            std::vector<Theme> v(4);
            v[0].name = "default";
            v[1].name = "high-contrast";
            v[1].c = {Hex("#FFFF00"), Hex("#00FFFF"), Hex("#FFFFFF"), Hex("#FFFFFF"), Hex("#FFFFFF"), Hex("#FF00FF"), Hex("#FF8000")};
            v[1].boxStyle = 1;
            v[1].lineWidth = 3;
            v[1].opacity = 1;
            v[2].name = "colorblind";
            v[2].c = {Hex("#FF8C00"), Hex("#1E90FF"), Hex("#FFFFFF"), Hex("#E0E0E0"), Hex("#FFFFFF"), Hex("#FFD700"), Hex("#FF8C00")};
            v[3].name = "soft";
            v[3].c = {Hex("#E06C75"), Hex("#98C379"), Hex("#ECEFF4"), Hex("#C0C6D4E6"), Hex("#ECEFF4"), Hex("#88C0D0"), Hex("#E06C75")};
            v[3].boxStyle = 0;
            v[3].lineWidth = 1;
            v[3].opacity = 0.75f;
            return v;
        }();
        return k;
    }
    inline const Theme* FindBuiltIn(const std::string& name)
    {
        for (const Theme& t : BuiltIn())
            if (t.name == name) return &t;
        return nullptr;
    }

    // ---- the themes folder ----
    // A file name made from a user's text: letters, digits, space, '-', '_', '.', at most 40 characters, never empty, no leading/trailing dot or space,
    // and not a name Windows reserves (CON, NUL, COM1 ...). Everything else becomes '_'.
    inline std::string SanitizeName(const std::string& in)
    {
        std::string s;
        for (unsigned char ch : in)
        {
            if (s.size() >= kMaxNameLen) break;
            s += (std::isalnum(ch) || ch == ' ' || ch == '-' || ch == '_' || ch == '.') ? static_cast<char>(ch) : '_';
        }
        while (!s.empty() && (s.back() == '.' || s.back() == ' ')) s.pop_back();
        size_t a = 0;
        while (a < s.size() && (s[a] == '.' || s[a] == ' ')) ++a;
        s.erase(0, a);
        if (s.empty()) return "theme";
        std::string up;
        for (char c : s.substr(0, s.find('.'))) up += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        static const char* const kReserved[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
                                                "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
        for (const char* r : kReserved)
            if (up == r) return "_" + s;
        return s;
    }

    // Names of the user's themes (file stems, sorted case-insensitively). A missing folder is an empty list.
    inline std::vector<std::string> List(const std::filesystem::path& dir)
    {
        std::vector<std::string> v;
        std::error_code ec;
        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        {
            if (!it->is_regular_file(ec) || it->path().extension() != ".json") continue;
            v.push_back(it->path().stem().string());
        }
        std::sort(v.begin(), v.end(), [](const std::string& a, const std::string& b) {
            return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) { return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y)); });
        });
        return v;
    }

    inline bool Save(const std::filesystem::path& dir, const Theme& t, std::string* error = nullptr, std::string* savedAs = nullptr)
    {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        const std::string stem = SanitizeName(t.name.empty() ? "theme" : t.name);
        Theme out = t;
        if (out.name.empty()) out.name = stem;
        if (savedAs) *savedAs = stem;
        return configstore::WriteAtomic(dir / (stem + ".json"), ToJson(out), error);
    }

    // Loads dir/<name>.json (a user theme) as a parse result; a missing or unreadable file is ok=false with a warning.
    inline ParseResult Load(const std::filesystem::path& dir, const std::string& name)
    {
        ParseResult r;
        const std::filesystem::path p = dir / (SanitizeName(name) + ".json");
        std::error_code ec;
        const auto size = std::filesystem::file_size(p, ec);
        if (ec)
        {
            r.warnings.push_back("no such theme");
            return r;
        }
        if (size > kMaxFileBytes)
        {
            r.warnings.push_back("the theme file is larger than " + std::to_string(kMaxFileBytes / 1024) + " KB - not read");
            return r;
        }
        std::ifstream f(p, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        r = Parse(ss.str());
        if (r.theme.name.empty()) r.theme.name = name;
        return r;
    }

    inline bool Delete(const std::filesystem::path& dir, const std::string& name)
    {
        std::error_code ec;
        return std::filesystem::remove(dir / (SanitizeName(name) + ".json"), ec) && !ec;
    }
} // namespace theme
