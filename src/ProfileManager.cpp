#include "ProfileManager.h"
#include "ConfigSchema.h"
#include "ConfigStore.h"
#include "Logger.h"
#include "Paths.h"
#include "Presets.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace ProfileManager
{
    namespace
    {
        std::vector<std::string> g_names;
        std::string g_lastError;
        std::filesystem::path Dir() { return ConfigDir() / "profiles"; }
        std::filesystem::path PathFor(const std::string& n) { return Dir() / (n + ".json"); }

        bool EnsureDir()
        {
            std::error_code ec;
            std::filesystem::create_directories(Dir(), ec);
            if (ec)
            {
                g_lastError = "Cannot create profiles/ directory: " + ec.message();
                LOG_ERROR("ProfileManager: %s", g_lastError.c_str());
                return false;
            }
            return true;
        }
    } // namespace

    const std::vector<std::string>& ProfileNames() { return g_names; }
    const std::string& LastError() { return g_lastError; }

    void Refresh()
    {
        g_lastError.clear();
        g_names.clear();
        if (!EnsureDir()) return;
        try
        {
            for (const auto& e : std::filesystem::directory_iterator(Dir()))
                if (e.is_regular_file() && e.path().extension() == ".json" && !e.path().stem().empty())
                    g_names.push_back(e.path().stem().string());
            std::sort(g_names.begin(), g_names.end(), [](const std::string& a, const std::string& b)
            {
                return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                                                    [](unsigned char x, unsigned char y)
                                                    {
                                                        return std::tolower(x) < std::tolower(y);
                                                    });
            });
        }
        catch (const std::exception& e)
        {
            g_lastError = std::string("Cannot read profiles/ directory: ") + e.what();
            LOG_ERROR("ProfileManager.Refresh: %s", g_lastError.c_str());
            g_names.clear();
        }
    }

    bool Save(const std::string& name, const EspConfig& cfg)
    {
        return SaveAs(name, cfg, true);
    }

    bool SaveAs(const std::string& name, const EspConfig& cfg, bool makeActive)
    {
        g_lastError.clear();
        // Same rule as the C# tool: letters, digits, '_', '-', ' ' only.
        std::string safe;
        for (unsigned char c : name)
            if (std::isalnum(c) || c == '_' || c == '-' || c == ' ') safe += static_cast<char>(c);
        size_t a = safe.find_first_not_of(' '), b = safe.find_last_not_of(' ');
        safe = a == std::string::npos ? "" : safe.substr(a, b - a + 1);
        if (safe.empty())
        {
            g_lastError = "Profile name contains no valid characters.";
            LOG_WARN("ProfileManager.Save: %s", g_lastError.c_str());
            return false;
        }
        if (!EnsureDir()) return false;

        std::string err;
        configstore::WriteOptions o;
        o.keepBackup = false; // a profile is one click away from being saved again
        if (!configstore::WriteAtomic(PathFor(safe), cfg.ToJson(), &err, o))
        {
            g_lastError = "IO error writing profile: " + err;
            LOG_ERROR("ProfileManager.Save: %s", g_lastError.c_str());
            return false;
        }
        LOG_INFO("ProfileManager.Save: saved profile '%s'.", safe.c_str());
        Refresh();
        if (makeActive) SetActive(safe);
        return true;
    }

    std::optional<EspConfig> Load(const std::string& name)
    {
        g_lastError.clear();
        std::error_code ec;
        if (!std::filesystem::exists(PathFor(name), ec))
        {
            g_lastError = "Profile '" + name + "' not found.";
            LOG_WARN("ProfileManager.Load: %s", g_lastError.c_str());
            return std::nullopt;
        }
        try
        {
            std::ifstream in(PathFor(name));
            std::stringstream ss;
            ss << in.rdbuf();
            bool parsed = false;
            const configschema::LoadResult r = configschema::LoadText(ss.str(), &parsed);
            if (!parsed)
            {
                g_lastError = "Profile '" + name + "' contains malformed JSON: " + (r.issues.empty() ? std::string() : r.issues.front().message);
                LOG_ERROR("ProfileManager.Load: %s", g_lastError.c_str());
                return std::nullopt;
            }
            for (const auto& i : r.issues)
                if (i.level == configschema::Issue::Level::Warning) LOG_WARN("ProfileManager.Load('%s'): %s", name.c_str(), i.message.c_str());
            LOG_INFO("ProfileManager.Load: loaded profile '%s' (%s).", name.c_str(), r.Summary().c_str());
            return r.cfg; // missing keys default, unknown keys are kept -> old profiles keep working
        }
        catch (const std::exception& e)
        {
            g_lastError = "Unexpected error loading profile '" + name + "': " + e.what();
            LOG_ERROR("ProfileManager.Load: %s", g_lastError.c_str());
            return std::nullopt;
        }
    }

    bool Delete(const std::string& name)
    {
        g_lastError.clear();
        std::error_code ec;
        if (!std::filesystem::exists(PathFor(name), ec))
        {
            LOG_INFO("ProfileManager.Delete: '%s' does not exist, nothing to do.", name.c_str());
            return false;
        }
        if (!std::filesystem::remove(PathFor(name), ec))
        {
            g_lastError = "Cannot delete '" + name + "': " + ec.message();
            LOG_ERROR("ProfileManager.Delete: %s", g_lastError.c_str());
            return false;
        }
        LOG_INFO("ProfileManager.Delete: deleted profile '%s'.", name.c_str());
        Refresh();
        return true;
    }

    // ---------------- built-ins ----------------
    const std::vector<std::string>& BuiltInNames()
    {
        static const std::vector<std::string> n = {"default", "testing", "low-power"};
        return n;
    }

    EspConfig BuiltIn(const std::string& name)
    {
        EspConfig c; // "default"
        if (name == "testing")
        {
            // everything visible and described, nothing hidden by distance: to check that the overlay sees what the game sees
            c.ShowPlayers = c.ShowPlayerNames = c.ShowHealth = true;
            c.ShowItems = c.ShowItemNames = c.ShowItemCount = c.ShowItemEnchants = c.ShowItemTags = true;
            c.HideJunk = false;
            c.ShowTracers = true;
            c.ShowMinimap = true;
            c.MaxLabelDist = -1; // -1 = unlimited
            c.MaxPlayerDist = -1;
        }
        else if (name == "low-power")
        {
            // a laptop on battery: fewer frames, fewer layers, vsync instead of spinning
            c.FpsCap = 60;
            c.AgentHz = 60; // half the samples: less work in the game too
            c.VSync = true;
            c.ShowMinimap = false;
            c.ShowTracers = false;
            c.TextShadow = false;
            c.ShowItemEnchants = false;
            c.ShowItemTags = false;
        }
        return c;
    }

    int SeedBuiltIns()
    {
        std::error_code ec;
        if (!EnsureDir()) return 0;
        const auto marker = Dir() / ".builtins-seeded";
        if (std::filesystem::exists(marker, ec)) return 0;
        int written = 0;
        for (const std::string& n : BuiltInNames())
        {
            if (std::filesystem::exists(PathFor(n), ec)) continue; // never overwrite a profile of the same name
            std::string err;
            configstore::WriteOptions o;
            o.keepBackup = false;
            if (configstore::WriteAtomic(PathFor(n), BuiltIn(n).ToJson(), &err, o)) ++written;
            else LOG_WARN("ProfileManager.SeedBuiltIns: %s: %s", n.c_str(), err.c_str());
        }
        std::string err;
        configstore::WriteOptions o;
        o.keepBackup = false;
        configstore::WriteAtomic(marker, "The built-in profiles were created once. Delete this file to create the missing ones again.\n", &err, o);
        if (written) LOG_INFO("ProfileManager: created %d built-in profile(s).", written);
        Refresh();
        return written;
    }

    // ---------------- active profile ----------------
    namespace
    {
        std::filesystem::path ActiveFile() { return ConfigDir() / "active_profile.txt"; }
    }

    std::string Find(const std::string& name)
    {
        auto lower = [](std::string s) { for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; };
        const std::string want = lower(name);
        for (const std::string& n : g_names) if (lower(n) == want) return n;
        return {};
    }

    std::string Active()
    {
        std::ifstream in(ActiveFile());
        std::string n;
        std::getline(in, n);
        while (!n.empty() && (n.back() == '\r' || n.back() == ' ')) n.pop_back();
        return Find(n); // a deleted profile is no longer active
    }

    void SetActive(const std::string& name)
    {
        std::string err;
        configstore::WriteOptions o;
        o.keepBackup = false;
        if (!configstore::WriteAtomic(ActiveFile(), name + "\n", &err, o)) LOG_WARN("ProfileManager.SetActive: %s", err.c_str());
    }

    std::string CycleName(const std::vector<std::string>& names, const std::string& current, int dir)
    {
        if (names.empty()) return {};
        const auto it = std::find(names.begin(), names.end(), current);
        const int n = static_cast<int>(names.size());
        if (it == names.end()) return dir >= 0 ? names.front() : names.back();
        const int i = static_cast<int>(it - names.begin());
        return names[static_cast<size_t>(((i + (dir >= 0 ? 1 : -1)) % n + n) % n)];
    }

    bool Activate(const std::string& name, Settings& s)
    {
        auto cfg = Load(name);
        if (!cfg) return false;
        s.Apply(*cfg);
        SetActive(name);
        s.RequestSave();
        return true;
    }
} // namespace ProfileManager
