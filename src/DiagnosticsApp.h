// DiagnosticsApp.h - gathers what the report needs from the running program / the disk and hands it to diag::Build.
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace diag
{
    struct AppContext
    {
        std::string version; // ESP_VERSION
        std::string renderer, settings, hudStatus, streamStats; // empty when the overlay is not running (--diagnostics)
        std::filesystem::path agentLog; // esp_agent.log (the jar's folder)
        std::vector<std::string> playerNames; // names seen in the current snapshot (empty offline)
        std::string localName;
        std::string token; // the session token, if any: it is removed from the report
    };

    // Builds the redacted report: system facts, redaction setup (user/host name, profile and exe folders, friends, player names, names found in
    // the logs, the token), the tails of esp_log.txt (+ esp_log.1.txt) and esp_agent.log.
    std::string Collect(const AppContext& ctx);
} // namespace diag
