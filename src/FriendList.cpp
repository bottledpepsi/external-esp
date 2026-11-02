#include "FriendList.h"
#include "ConfigStore.h"
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
        const configstore::ReadResult rr = configstore::Read(FilePath(), configstore::IsJsonStringArray);
        switch (rr.status)
        {
        case configstore::ReadStatus::Missing:
            LOG_INFO("FriendList: friends.json not found - starting empty.");
            return;
        case configstore::ReadStatus::Unreadable:
        case configstore::ReadStatus::Damaged:
            LOG_ERROR("FriendList: %s", rr.note.c_str());
            return;
        case configstore::ReadStatus::RestoredFromBackup: LOG_WARN("FriendList: %s", rr.note.c_str());
            break;
        case configstore::ReadStatus::Ok: break;
        }
        auto list = json::ParseStringArray(rr.text);
        if (!list) return; // cannot happen: the validator accepted it
        g_names = std::set<std::string, CiLess>(list->begin(), list->end());
        LOG_INFO("FriendList: loaded %llu friend(s).", static_cast<unsigned long long>(g_names.size()));
    }

    static void SaveLocked()
    {
        std::string text = "[\n";
        size_t i = 0;
        for (const auto& n : g_names) text += "  " + json::Escape(n) + (++i < g_names.size() ? ",\n" : "\n");
        text += "]\n";
        std::string err;
        configstore::WriteOptions o;
        o.validPrevious = configstore::IsJsonStringArray;
        if (configstore::WriteAtomic(FilePath(), text, &err, o)) LOG_INFO("FriendList: saved %llu friend(s).", static_cast<unsigned long long>(g_names.size()));
        else LOG_ERROR("FriendList: saving friends.json failed - %s", err.c_str());
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
