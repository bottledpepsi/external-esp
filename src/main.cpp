// external-esp (C++ port) - external Direct2D overlay for a LOCAL Minecraft Java world.
//
//   Minecraft JVM <--(JVM Attach API)-- esp-agent.jar --UDP 127.0.0.1:47615--> UdpListener --> EspOverlay (D2D layered window)
//
// Usage:
//   mcesp [--demo] [--pid N] [--port N] [--java PATH] [--jar PATH] [--no-inject] [--open-config] [--screen] [--gdi] [--hz N] [--list] [--record FILE]
//   Insert = config window, Delete = toggle ESP, hold End = exit (all rebindable in the HOTKEYS tab).
//
// Scope: written for single-player / LAN worlds you control (two accounts on your own machine) -
// reverse-engineering practice, not for use on other people's servers.
#include "AgentInjector.h"
#include "App.h"
#include "EspOverlay.h"
#include "FriendList.h"
#include "Logger.h"
#include "Paths.h"
#include "Token.h"
#include "Wire.h"
#include "ProcessManager.h"
#include "ProfileManager.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstdlib>
#include <string>

namespace
{
    void PrintUsage()
    {
        std::puts(
            "external-esp (C++)\n"
            "  mcesp [--demo] [--pid N] [--port N] [--java PATH] [--jar PATH] [--no-inject] [--open-config] [--screen] [--gdi] [--hz N] [--list] [--record FILE] [--replay FILE [--replay-speed X] [--replay-loop]] [--token T | --no-auth]\n"
            "\n"
            "  --demo       synthetic players (test the overlay/config UI without Minecraft)\n"
            "  --pid N      attach to this java(w).exe PID (two Minecraft clients on one PC)\n"
            "  --port N     UDP port for the agent stream (default 47615)\n"
            "  --java PATH  java.exe of a JDK to run the attach helper (default: the game's own JRE, JAVA_HOME, PATH)\n"
            "  --jar PATH   agent jar (default: esp-agent.jar next to the exe)\n"
            "  --no-inject  do not attach automatically (the exact -javaagent line, with the session token, is printed at start-up)\n"
            "  --open-config  open the config window right away\n"
            "  --screen     cover the whole primary monitor instead of tracking the game window\n"
            "  --hz N       agent snapshot rate, 1-240 (default 120; more = smoother boxes, a little more CPU in the game)\n"
            "  --gdi        use the legacy CPU renderer (layered window) instead of the GPU swap chain\n"
            "  --record F   also save the raw agent datagrams + arrival times to F (.esprec) for --replay / tests / bug reports\n"
            "  --replay F   play back an .esprec recording over the whole screen instead of attaching to Minecraft\n"
            "               (F6 pause/resume, F7 step while paused, F8 slower, F9 faster); --replay-speed X, --replay-loop\n"
            "  --token T    use this session token instead of a random one (16-64 letters/digits; for --no-inject: pass the same to the agent)\n"
            "  --no-auth    accept datagrams without / with any session token (not recommended)\n"
            "  --list       list running Minecraft/java processes and exit\n"
            "\nKeys: Insert config | Delete toggle | hold End exit");
    }
} // namespace

int main(int argc, char** argv)
{
    SetProcessDPIAware(); // overlay pixels must equal game-window pixels on scaled displays
    elog::Init();

    AppOptions opt;
    opt.jarPath = AppDir() / "esp-agent.jar";
    bool list = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--help" || a == "-h")
        {
            PrintUsage();
            return 0;
        }
        else if (a == "--demo") opt.demo = true;
        else if (a == "--no-inject") opt.autoInject = false;
        else if (a == "--list") list = true;
        else if (a == "--open-config") opt.openConfig = true;
        else if (a == "--screen") opt.screenOverlay = true;
        else if (a == "--gdi") opt.legacyGfx = true;
        else if (a == "--hz" && i + 1 < argc) opt.agentHz = static_cast<int>(std::clamp(std::strtol(argv[++i], nullptr, 10), 1L, 240L));
        else if (a == "--record" && i + 1 < argc) opt.recordPath = argv[++i];
        else if (a == "--token" && i + 1 < argc) opt.token = argv[++i];
        else if (a == "--no-auth") opt.requireAuth = false;
        else if (a == "--replay" && i + 1 < argc) opt.replayPath = argv[++i];
        else if (a == "--replay-speed" && i + 1 < argc) opt.replaySpeed = std::atof(argv[++i]);
        else if (a == "--replay-loop") opt.replayLoop = true;
        else if (a == "--pid" && i + 1 < argc) opt.pid = static_cast<DWORD>(std::strtoul(argv[++i], nullptr, 10));
        else if (a == "--port" && i + 1 < argc) opt.port = static_cast<uint16_t>(std::strtoul(argv[++i], nullptr, 10));
        else if (a == "--java" && i + 1 < argc) opt.javaPath = argv[++i];
        else if (a == "--jar" && i + 1 < argc) opt.jarPath = argv[++i];
        else
        {
            PrintUsage();
            return 1;
        }
    }

    if (list)
    {
        const auto procs = ProcessManager::Scan();
        if (procs.empty()) std::puts("No java/javaw processes found.");
        for (const auto& p : procs)
            std::printf("PID %lu  %-28s  window: '%s'\n         JRE: %s\n", static_cast<unsigned long>(p.pid),
                        p.DisplayName().c_str(), p.title.c_str(), p.javaExe.string().c_str());
        return 0;
    }

    if (opt.demo && !opt.recordPath.empty())
    {
        std::puts("--record needs the real agent stream; --demo has no datagrams to record.");
        return 1;
    }
    if (!opt.replayPath.empty())
    {
        if (opt.demo || !opt.recordPath.empty())
        {
            std::puts("--replay cannot be combined with --demo or --record.");
            return 1;
        }
        opt.screenOverlay = true; // no game window to follow: cover the whole monitor like --screen
        opt.autoInject = false; // and never touch a running Minecraft
    }
    if (!opt.token.empty() && !token::IsValid(opt.token))
    {
        std::puts("--token must be 16-64 letters/digits.");
        return 1;
    }
    if (opt.token.empty() && opt.requireAuth && !opt.demo && opt.replayPath.empty())
    {
        opt.token = token::Generate(); // 128 random bits from the OS
        if (opt.token.empty())
        {
            std::puts("Cannot create a session token (the OS random number generator failed); refusing to start without one. Use --no-auth to run unauthenticated.");
            return 1;
        }
    }
    if (!opt.autoInject && !opt.demo && opt.replayPath.empty())
        std::printf("Start the game with:  -javaagent:esp-agent.jar=port=%d;hz=%d;wire=%d%s%s\n", static_cast<int>(opt.port), opt.agentHz, wire::kMaxVersion,
                    opt.token.empty() ? "" : ";token=", opt.token.c_str());
    FriendList::Load();
    ProfileManager::Refresh();
    LOG_INFO("Mode: %s | UDP port %u | agent jar: %s", opt.demo ? "DEMO" : "agent", static_cast<unsigned>(opt.port),
             opt.jarPath.string().c_str());

    EspOverlay overlay(opt);
    return overlay.Run();
}
