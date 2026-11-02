// LegacyImport.h - one-time copy of the settings that older builds kept next to the executable (esp_config.json, friends.json, profiles\*.json)
// into the per-user config folder (%APPDATA%\external-esp). Portable: both folders are parameters, so the tests run it anywhere.
//
// Rules: nothing is moved or deleted (an older exe keeps working with its own files); nothing is overwritten; the import runs once - a marker
// file in the new folder records that it was considered, so deleting the config to start over does not bring the old one back.
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace legacy
{
    inline constexpr const char* kMarkerFile = ".imported-from-exe-folder";

    struct Result
    {
        bool ran = false; // false: nothing to do (same folder, or already considered, or the new folder is not empty)
        int copied = 0;
        int failed = 0;
        std::vector<std::string> notes; // one line per file, for the log
    };

    Result ImportFromExeFolder(const std::filesystem::path& exeDir, const std::filesystem::path& configDir);
} // namespace legacy
