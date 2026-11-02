#include "FriendList.h"
#include "Json.h"
#include "Logger.h"
#include "Paths.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>

namespace FriendList
{
    namespace
    {
        struct CiLess
        {
            bool operator()(const std::string& a, const std::string& b) const
            {
                return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                                                    [](unsigned char x, unsigned char y)
                                                    {
                                                        return std::tolower(x) < std::tolower(y);
                                                    });
            }
        };

        std::mutex g_mtx;
        // Contains() is called from the render path, Add/Remove from the UI path (same thread today; cheap insurance)
        std::set<std::string, CiLess> g_names;
        std::filesystem::path FilePath() { return ConfigDir() / "friends.json"; }

        std::string Trim(const std::string& s)
        {
            size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
            return a == std::string::npos ? "" : s.substr(a, b - a + 1);
        }
    } // namespace

    void Load()
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        std::error_code ec;
        if (!std::filesystem::exists(FilePath(), ec))
        {
            LOG_INFO("FriendList: friends.json not found - starting empty.");
            return;
        }
        std::ifstream in(FilePath());
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string text = ss.str();
        if (text.find_first_not_of(" \t\r\n") == std::string::npos)
        {
            LOG_WARN("FriendList: friends.json is empty - starting empty.");
            return;
        }
        std::string err;
        auto list = json::ParseStringArray(text, &err);
        if (!list)
        {
            LOG_ERROR("FriendList: malformed JSON in friends.json - %s", err.c_str());
            return;
        }
        g_names = std::set<std::string, CiLess>(list->begin(), list->end());
        LOG_INFO("FriendList: loaded %llu friend(s).", static_cast<unsigned long long>(g_names.size()));
    }

    static void SaveLocked()
    {
        auto tmp = FilePath();
        tmp += ".tmp";
        try
        {
            std::filesystem::create_directories(FilePath().parent_path()); // %APPDATA%\\external-esp does not exist before the first save
            {
                std::ofstream out(tmp, std::ios::trunc);
                out << "[\n";
                size_t i = 0;
                for (const auto& n : g_names) out << "  " << json::Escape(n) << (++i < g_names.size() ? ",\n" : "\n");
                out << "]\n";
                if (!out) throw std::runtime_error("write failed");
            }
            std::filesystem::rename(tmp, FilePath());
            LOG_INFO("FriendList: saved %llu friend(s).", static_cast<unsigned long long>(g_names.size()));
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("FriendList: saving friends.json failed - %s", e.what());
            std::error_code ec;
            std::filesystem::remove(tmp, ec);
        }
    }

    void Save()
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        SaveLocked();
    }

    bool Add(const std::string& name)
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        const std::string t = Trim(name);
        if (t.empty())
        {
            LOG_WARN("FriendList.Add: ignoring blank name.");
            return false;
        }
        if (!g_names.insert(t).second)
        {
            LOG_INFO("FriendList.Add: '%s' is already a friend.", t.c_str());
            return false;
        }
        LOG_INFO("FriendList.Add: added '%s'.", t.c_str());
        SaveLocked();
        return true;
    }

    bool Remove(const std::string& name)
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        if (g_names.erase(name) == 0)
        {
            LOG_WARN("FriendList.Remove: '%s' was not in the friend list.", name.c_str());
            return false;
        }
        LOG_INFO("FriendList.Remove: removed '%s'.", name.c_str());
        SaveLocked();
        return true;
    }

    bool Contains(const std::string& name)
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        return g_names.count(name) != 0;
    }

    std::vector<std::string> Names()
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        return {g_names.begin(), g_names.end()};
    }
} // namespace FriendList
