// ControlFile.h - how the overlay talks to the agent: a one-line request in a small file (the agent's option ctl=<path>), rewritten every couple of seconds
// while it is wanted; the file's modification time is the keep-alive (the agent stops inspecting when it has not been renewed for a few seconds).
// The answer comes back in the normal datagram stream (wire v6 "X" lines).
//
//   inspect <id>     the entity with that E-line id         inspect player     the agent's own discovery paths       inspect off
//
// Portable (no Windows headers).
#pragma once
#include "ConfigStore.h"
#include <cstdint>
#include <filesystem>
#include <string>

namespace controlfile
{
    inline constexpr const char* kFileName = "agent.ctl";
    inline constexpr double kRenewSeconds = 2.0; // the overlay rewrites a live request this often; the agent lets one lapse after 6 s

    inline std::string RequestEntity(uint32_t id) { return "inspect " + std::to_string(id) + "\n"; }
    inline std::string RequestAgent() { return "inspect player\n"; }
    inline std::string RequestOff() { return "inspect off\n"; }

    // The text to add to the agent's option string ("ctl=..."), or "" when the path cannot be passed safely: the options are split at ';' and '=', and
    // control characters or an empty / relative path would only confuse the agent (which would then ignore the option). Without it the inspector is off.
    inline std::string AgentOption(const std::filesystem::path& file)
    {
        const std::string p = file.string();
        if (p.empty() || p.size() > 260 || !file.is_absolute()) return "";
        for (unsigned char c : p)
            if (c < 32 || c == 127 || c == ';' || c == '=') return "";
        return "ctl=" + p;
    }

    // Writes (and so touches) the file. Its folder is made when missing.
    inline bool Write(const std::filesystem::path& file, const std::string& text, std::string* error = nullptr)
    {
        std::error_code ec;
        if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
        return configstore::WriteAtomic(file, text, error);
    }
} // namespace controlfile
