// Paths.h - where things live.
//   AppDir()    : the folder of the executable (the jar, the logs, libwinpthread-1.dll).
//   ConfigDir() : the per-user settings folder - %APPDATA%\external-esp - with esp_config.json, friends.json and profiles\.
//                 The folder name is ONE constant (paths::kAppFolderName); change it here and nowhere else.
#pragma once
#include <filesystem>
#include <string>
#ifdef _WIN32
#include "Platform.h"
#endif

namespace paths
{
    // The folder under %APPDATA% (and %LOCALAPPDATA%) that belongs to this program.
    inline constexpr const char* kAppFolderName = "external-esp";
} // namespace paths

inline std::filesystem::path AppDir()
{
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof buf / sizeof buf[0]));
    return std::filesystem::path(std::wstring(buf, n)).parent_path();
#else
    return std::filesystem::current_path(); // host-side unit tests
#endif
}

namespace paths
{
    // Tests (and nothing else) point the config folder somewhere else with this.
    inline std::filesystem::path& ConfigDirOverride()
    {
        static std::filesystem::path p;
        return p;
    }

    // %<envVar>%\external-esp, or `fallback` when the variable is not set (a stripped-down environment: services, some test runners).
    inline std::filesystem::path UserFolder(const wchar_t* envVar, const std::filesystem::path& fallback)
    {
#ifdef _WIN32
        wchar_t buf[MAX_PATH * 2] = {};
        const DWORD n = GetEnvironmentVariableW(envVar, buf, static_cast<DWORD>(sizeof buf / sizeof buf[0]));
        if (n > 0 && n < sizeof buf / sizeof buf[0]) return std::filesystem::path(std::wstring(buf, n)) / kAppFolderName;
#else
        (void)envVar;
#endif
        return fallback;
    }
} // namespace paths

// %APPDATA%\external-esp (the folder is created by whoever writes into it, see ConfigStore / EnsureConfigDir).
// Off Windows (the host-side tests) it is the current directory, so a test that runs in an empty folder sees an empty config folder.
inline std::filesystem::path ConfigDir()
{
    if (!paths::ConfigDirOverride().empty()) return paths::ConfigDirOverride();
#ifdef _WIN32
    return paths::UserFolder(L"APPDATA", AppDir());
#else
    return AppDir();
#endif
}
