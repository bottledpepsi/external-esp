// App.h - command-line options shared by main / supervisor / overlay.
#pragma once
#include "Platform.h"
#include <cstdint>
#include <filesystem>
#include <string>

struct AppOptions
{
    bool demo = false; // synthetic players, no Minecraft needed (UI / projection testing)
    DWORD pid = 0; // attach to this PID (two clients on one PC)
    uint16_t port = 47615; // UDP port the agent sends to
    bool autoInject = true; // load the agent automatically
    std::string javaPath; // optional java.exe override for the attach helper
    std::filesystem::path jarPath; // esp-agent.jar (default: next to the exe)
    int agentHz = 120; // snapshot rate requested from the agent (1..240); higher = less extrapolation = smoother boxes
    bool screenOverlay = false; // cover the whole primary monitor instead of tracking the game window
    bool openConfig = false; // open the config window as soon as the overlay is visible
    std::string token; // session token (wire v5): generated at start-up unless --token / --no-auth
    bool verbose = false; // --verbose: DEBUG logging in the overlay and (option verbose=1) in the agent
    bool requireAuth = true; // --no-auth: accept datagrams without / with any token
    std::filesystem::path replayPath; // --replay FILE: play an .esprec recording instead of listening to the agent
    double replaySpeed = 1.0;         // --replay-speed X
    bool replayLoop = false;          // --replay-loop
    std::filesystem::path recordPath; // --record FILE: tee the raw agent datagrams into an .esprec file (docs/esprec.md)
    std::string agentPacer; // --agent-pacer sleep|hybrid: how the agent waits for its next tick (default: the agent's own default, sleep)
    int pacingSeconds = 0; // --pacing-report [S]: the agent writes esp_pacing.txt after S seconds (docs/pacing.md)
    bool legacyGfx = false; // --gdi: force the old DIB + UpdateLayeredWindow renderer (default: GPU swap chain)

    // Agent options beyond the common ones, as ";key=value" pieces, shared by the automatic attach and the printed -javaagent line.
    std::string agentExtraOptions() const
    {
        std::string o;
        if (!agentPacer.empty()) o += ";pacer=" + agentPacer;
        if (pacingSeconds > 0) o += ";pacing=" + std::to_string(pacingSeconds);
        return o;
    }
};
