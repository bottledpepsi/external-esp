// ProfileManager.h - port of ProfileManager.cs: named config presets in profiles/<name>.json in ConfigDir() (%APPDATA%\external-esp).
// Non-throwing; errors go to the log and to LastError() (shown in the UI).
#pragma once
#include "EspConfig.h"
#include <optional>
#include <string>
#include <vector>

namespace ProfileManager
{
    void Refresh(); // rescan profiles/ (sorted, case-insensitive)
    const std::vector<std::string>& ProfileNames();
    const std::string& LastError();
    bool Save(const std::string& name, const EspConfig& cfg); // sanitises the name; atomic write
    std::optional<EspConfig> Load(const std::string& name);
    bool Delete(const std::string& name);
} // namespace ProfileManager
