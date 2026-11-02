#include "ConfigSchema.h"
#include "Hotkeys.h"
#include "Presets.h"
#include "Tunables.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace configschema
{
    namespace
    {
        enum class Kind { Bool, Int, Float, Word, Hotkey };

        struct Spec
        {
            const char* key;
            Kind kind;
            double min = 0, max = 0; // Int / Float
            std::vector<std::string> words; // Word
        };

        const std::vector<Spec>& Table()
        {
            static const std::vector<Spec> t = []
            {
                auto mm = [](const auto& v) { return std::pair<double, double>{*std::min_element(v.begin(), v.end()), *std::max_element(v.begin(), v.end())}; };
                std::vector<Spec> s;
                auto B = [&](const char* k) { s.push_back({k, Kind::Bool, 0, 0, {}}); };
                auto I = [&](const char* k, double lo, double hi) { s.push_back({k, Kind::Int, lo, hi, {}}); };
                auto F = [&](const char* k, double lo, double hi) { s.push_back({k, Kind::Float, lo, hi, {}}); };
                auto W = [&](const char* k, std::vector<std::string> w) { s.push_back({k, Kind::Word, 0, 0, std::move(w)}); };
                for (int i = 0; i < hotkeys::kActionCount; ++i) s.push_back({hotkeys::Def(hotkeys::ActionAt(i)).key, Kind::Hotkey, 0, 0, {}});
                W("DistanceFormat", {"Meters", "Kilometers", "Off"});
                I("MaxLabelDist", -1, 100000);
                B("TextShadow");
                B("ShowPlayers"); B("ShowPlayerNames");
                W("BoxStyle", {"Rounded", "Sharp", "Corner-only", "Skeleton"});
                { const auto r = mm(presets::CornerRadius); F("CornerRadius", r.first, r.second); }
                { const auto r = mm(presets::Thickness); I("BoxThickness", r.first, r.second); }
                W("BoxColorMode", {"Static", "Gradient"});
                { const auto r = mm(presets::NameSizes); I("NameSize", r.first, r.second); }
                { const auto r = mm(presets::Opacities); F("BoxFillOpacity", r.first, r.second); }
                B("ShowTracers");
                W("TracerOrigin", {"Bottom", "Center"});
                W("TracerColor", {"Dynamic", "White"});
                { const auto r = mm(presets::TraceOpacities); F("TracerOpacity", r.first, r.second); }
                { const auto r = mm(presets::Thickness); I("TracerThickness", r.first, r.second); }
                W("TracerTarget", {"Feet", "Center", "Head"});
                B("TracerFade");
                W("HudMode", {"Off", "Compact", "Detailed"});
                B("ShowItems"); B("ShowItemNames"); B("ShowItemCount");
                { const auto r = mm(presets::TraceOpacities); F("ItemOpacity", r.first, r.second); }
                B("HideJunk");
                I("MaxPlayerDist", -1, 100000);
                B("ShowMinimap");
                { const auto r = mm(presets::MinimapSizes); I("MinimapSize", r.first, r.second); }
                { const auto r = mm(presets::MinimapRanges); I("MinimapRange", r.first, r.second); }
                { const auto r = mm(presets::OverlayOpacity); F("OverlayOpacity", r.first, r.second); }
                I("FpsCap", 0, 1000);
                F("ConfigX", -20000, 20000); F("ConfigY", -20000, 20000);
                { const auto r = mm(presets::Fovs); I("Fov", r.first, r.second); }
                B("VSync"); B("Smoothing"); B("ShowHealth"); B("ShowItemEnchants"); B("ShowItemTags");
                // the ADVANCED tab's numbers: ranges come from Tunables.h, the one table the UI uses too
                for (int i = 0; i < tunables::Count(); ++i)
                {
                    const tunables::Def& d = tunables::Get(static_cast<tunables::Id>(i));
                    if (d.maxIsUnlimited) continue; // MaxLabelDist is checked above (-1 or a distance)
                    s.push_back({d.key, d.id == tunables::Id::AgentHz ? Kind::Int : Kind::Float, d.min, d.max, {}});
                }
                return s;
            }();
            return t;
        }

        const EspConfig& Defaults()
        {
            static const EspConfig d;
            return d;
        }

        // the default of a Word / KeyName key, read from the default config's own JSON, so the table cannot disagree with EspConfig
        std::string DefaultText(const std::string& key)
        {
            auto o = json::ParseObject(Defaults().ToJson());
            return o ? json::GetString(*o, key.c_str(), "") : std::string();
        }

        const char* TypeName(json::Value::Type t)
        {
            switch (t)
            {
            case json::Value::Type::Bool: return "true/false";
            case json::Value::Type::Number: return "a number";
            case json::Value::Type::String: return "text";
            default: return "something else (null, a list, an object or a broken number)";
            }
        }

        std::string Num(double v) { return json::Number(v); }

        std::string Join(const std::vector<std::string>& w)
        {
            std::string s;
            for (size_t i = 0; i < w.size(); ++i) s += (i ? ", " : "") + w[i];
            return s;
        }

        void Warn(std::vector<Issue>& out, const std::string& key, std::string msg) { out.push_back({Issue::Level::Warning, key, std::move(msg)}); }

        // Checks one key of `o` against its spec. A key that cannot be used is erased (so the reader takes the default); a number outside the
        // range is clamped in place.
        void Check(json::Object& o, const Spec& sp, std::vector<Issue>& out)
        {
            auto it = o.find(sp.key);
            if (it == o.end()) return;
            json::Value& v = it->second;
            const std::string key = sp.key;
            switch (sp.kind)
            {
            case Kind::Bool:
                if (v.type != json::Value::Type::Bool)
                {
                    Warn(out, key, key + ": expected true or false, found " + TypeName(v.type) + " - using the default");
                    o.erase(it);
                }
                break;
            case Kind::Int:
            case Kind::Float:
                if (v.type != json::Value::Type::Number)
                {
                    Warn(out, key, key + ": expected a number, found " + TypeName(v.type) + " - using the default");
                    o.erase(it);
                    break;
                }
                if (v.n < sp.min || v.n > sp.max)
                {
                    const double c = std::clamp(v.n, sp.min, sp.max);
                    Warn(out, key, key + ": " + Num(v.n) + " is outside " + Num(sp.min) + ".." + Num(sp.max) + " - using " + Num(c));
                    v.n = c;
                }
                break;
            case Kind::Word:
                {
                    const bool ok = v.type == json::Value::Type::String && std::find(sp.words.begin(), sp.words.end(), v.s) != sp.words.end();
                    if (!ok)
                    {
                        const std::string shown = v.type == json::Value::Type::String ? "'" + v.s + "'" : std::string(TypeName(v.type));
                        Warn(out, key, key + ": " + shown + " is not one of " + Join(sp.words) + " - using " + DefaultText(key));
                        o.erase(it);
                    }
                    break;
                }
            case Kind::Hotkey:
                {
                    hotkeys::Binding bind;
                    std::string why;
                    bool required = false;
                    for (int i = 0; i < hotkeys::kActionCount; ++i)
                        if (key == hotkeys::Def(hotkeys::ActionAt(i)).key) required = hotkeys::Def(hotkeys::ActionAt(i)).required;
                    if (v.type != json::Value::Type::String) why = std::string("expected text such as F6 or Ctrl+Shift+F5, found ") + TypeName(v.type);
                    else if (!hotkeys::Parse(v.s, bind, &why)) {}
                    else if (!bind.Bound() && required) why = "this hotkey cannot be left empty";
                    else if (const std::string p = hotkeys::Problem(bind); !p.empty()) why = p;
                    if (!why.empty())
                    {
                        Warn(out, key, key + ": " + why + " - using " + (DefaultText(key).empty() ? std::string("no key") : DefaultText(key)));
                        o.erase(it);
                    }
                    break;
                }
            }
        }

        // ---- the migration steps (index = the version they upgrade FROM) ----
        // v0 -> v1: the file had no version. Every key of a v0 file (the C# tool's, or an earlier C++ build's) means the same in v1, so there is
        // nothing to rewrite; the step exists so that "a file without a version" goes through the same path as every later upgrade, and so that
        // the version number is stamped when the file is next saved.
        void V0ToV1(json::Object&, std::vector<Issue>& issues)
        {
            issues.push_back({Issue::Level::Info, "", "the file has no schema version (written by the C# tool or an older build); read as version 1"});
        }
    } // namespace

    size_t LoadResult::Warnings() const
    {
        return static_cast<size_t>(std::count_if(issues.begin(), issues.end(), [](const Issue& i) { return i.level == Issue::Level::Warning; }));
    }

    std::string LoadResult::Summary() const
    {
        char b[120];
        if (fileVersion == kCurrentVersion) std::snprintf(b, sizeof b, "version %d, %zu warning(s)", fileVersion, Warnings());
        else std::snprintf(b, sizeof b, "version %d -> %d, %zu warning(s)", fileVersion, kCurrentVersion, Warnings());
        return b;
    }

    const std::vector<StepEntry>& Steps()
    {
        static const std::vector<StepEntry> s = {{0, V0ToV1}};
        return s;
    }

    void Migrate(json::Object& obj, int from, int to, const std::vector<StepEntry>& steps, std::vector<Issue>& issues)
    {
        for (int v = from; v < to; ++v)
        {
            const auto it = std::find_if(steps.begin(), steps.end(), [v](const StepEntry& e) { return e.from == v; });
            if (it == steps.end())
            {
                Warn(issues, "", "no migration from version " + std::to_string(v) + " to " + std::to_string(v + 1) + " - reading the keys as they are");
                continue;
            }
            it->fn(obj, issues);
        }
    }

    LoadResult Load(const json::Object& obj)
    {
        LoadResult r;
        json::Object o = obj;

        // version
        auto vit = o.find("Version");
        if (vit != o.end())
        {
            if (vit->second.type == json::Value::Type::Number && vit->second.n >= 0 && vit->second.n < 1e6 && vit->second.n == std::floor(vit->second.n))
                r.fileVersion = static_cast<int>(vit->second.n);
            else Warn(r.issues, "Version", "Version: expected a whole number - reading the file as version 0");
        }
        if (r.fileVersion > kCurrentVersion)
            Warn(r.issues, "Version", "the file was written by a newer build (version " + std::to_string(r.fileVersion) + ", this build reads " + std::to_string(kCurrentVersion) +
                 "): keys this build does not know are kept, and the next save writes version " + std::to_string(kCurrentVersion));
        else if (r.fileVersion < kCurrentVersion)
        {
            Migrate(o, r.fileVersion, kCurrentVersion, Steps(), r.issues);
            r.migrated = true;
        }

        for (const Spec& sp : Table()) Check(o, sp, r.issues);
        r.cfg = EspConfig::FromJson(o); // reads the checked keys, collects the unknown ones

        // two actions on the same key would both fire, or the second would never register: the earlier action keeps it
        for (int pass = 0; pass < 2; ++pass)
        {
            hotkeys::Table t{};
            for (int i = 0; i < hotkeys::kActionCount; ++i) hotkeys::Parse(r.cfg.HotkeyText(hotkeys::ActionAt(i)), t[static_cast<size_t>(i)]);
            const auto conflicts = hotkeys::FindConflicts(t);
            if (conflicts.empty()) break;
            const hotkeys::Conflict c = conflicts.front();
            const auto& d1 = hotkeys::Def(c.first);
            const auto& d2 = hotkeys::Def(c.second);
            std::string& second = r.cfg.HotkeyText(c.second);
            const std::string shown = second;
            hotkeys::Binding def2;
            hotkeys::Parse(d2.defaultText, def2);
            if (d2.required && def2 == t[static_cast<size_t>(c.first)])
            {
                // the second one's own default is what the first one was set to: both go back to their defaults
                r.cfg.HotkeyText(c.first) = d1.defaultText;
                second = d2.defaultText;
                Warn(r.issues, d2.key, std::string(d1.key) + " and " + d2.key + " both wanted " + shown + " - both use their defaults");
            }
            else if (d2.required)
            {
                second = d2.defaultText; // the second needs a key: it takes its default
                Warn(r.issues, d2.key, std::string(d2.key) + ": " + shown + " is also used by " + d1.key + " - using " + d2.defaultText);
            }
            else
            {
                second.clear();
                Warn(r.issues, d2.key, std::string(d2.key) + ": " + shown + " is also used by " + d1.key + " - " + d2.key + " is left unbound");
            }
        }

        // an unknown key is only kept when its text can be written back as valid JSON
        for (auto it = r.cfg.Unknown.begin(); it != r.cfg.Unknown.end();)
        {
            if (json::IsValid(it->second)) ++it;
            else
            {
                Warn(r.issues, it->first, it->first + ": a key this build does not know, and its value is not valid JSON - dropped");
                it = r.cfg.Unknown.erase(it);
            }
        }
        return r;
    }

    LoadResult LoadText(const std::string& text, bool* parsed)
    {
        std::string err;
        auto obj = json::ParseObject(text, &err);
        if (!obj)
        {
            if (parsed) *parsed = false;
            LoadResult r;
            r.fileVersion = -1;
            Warn(r.issues, "", "not a JSON object: " + (err.empty() ? std::string("unknown error") : err) + " - using the defaults");
            return r;
        }
        if (parsed) *parsed = true;
        return Load(*obj);
    }

    bool IsKnownKey(const std::string& key)
    {
        if (key == "Version") return true;
        for (const Spec& s : Table()) if (key == s.key) return true;
        return false;
    }

    std::vector<std::string> KnownKeys()
    {
        std::vector<std::string> k;
        for (const Spec& s : Table()) k.push_back(s.key);
        return k;
    }

    bool NumericRange(const std::string& key, Range& out)
    {
        for (const Spec& s : Table())
            if (key == s.key && (s.kind == Kind::Int || s.kind == Kind::Float))
            {
                out = {s.min, s.max};
                return true;
            }
        return false;
    }
} // namespace configschema
