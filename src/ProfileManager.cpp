#include "ProfileManager.h"
#include "ConfigSchema.h"
#include "Logger.h"
#include "Paths.h"
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

        auto path = PathFor(safe);
        auto tmp = path;
        tmp += ".tmp";
        try
        {
            {
                std::ofstream out(tmp, std::ios::trunc);
                out << cfg.ToJson();
                if (!out) throw std::runtime_error("write failed");
            }
            std::filesystem::rename(tmp, path);
            LOG_INFO("ProfileManager.Save: saved profile '%s'.", safe.c_str());
            Refresh();
            return true;
        }
        catch (const std::exception& e)
        {
            g_lastError = std::string("IO error writing profile: ") + e.what();
            LOG_ERROR("ProfileManager.Save: %s", g_lastError.c_str());
            std::error_code ec;
            std::filesystem::remove(tmp, ec);
            return false;
        }
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
} // namespace ProfileManager
