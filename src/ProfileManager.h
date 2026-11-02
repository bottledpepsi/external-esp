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
    bool Save(const std::string& name, const EspConfig& cfg); // sanitises the name; atomic write; the saved profile becomes the active one
    bool SaveAs(const std::string& name, const EspConfig& cfg, bool makeActive);
    std::optional<EspConfig> Load(const std::string& name);
    bool Delete(const std::string& name);

    // ---- built-in profiles ----
    // "default" (the program's defaults), "testing" (everything visible, item tags and health on) and "low-power" (60 fps cap, vsync, no minimap /
    // tracers / extras). SeedBuiltIns() writes them into an empty profiles folder once (a marker file stops it bringing back ones you deleted).
    const std::vector<std::string>& BuiltInNames();
    EspConfig BuiltIn(const std::string& name);
    int SeedBuiltIns(); // number of files written

    // ---- the active profile: the one most recently loaded or saved (shown in the UI, target of "update", start-up --profile) ----
    std::string Active(); // "" when none, or when that profile no longer exists
    void SetActive(const std::string& name);
    // Exact name (case-insensitive) as stored; "" when there is no such profile.
    std::string Find(const std::string& name);
    // next/previous name in `names`, wrapping; `current` not in the list -> the first (or last for dir < 0); empty list -> "".
    std::string CycleName(const std::vector<std::string>& names, const std::string& current, int dir);
    // Loads `name`, applies it to `s`, makes it active and asks for a save. False (and LastError) when it cannot be loaded.
    bool Activate(const std::string& name, Settings& s);
} // namespace ProfileManager
