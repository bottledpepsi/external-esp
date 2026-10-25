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
#include "ProcessManager.h"
#include "ProfileManager.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
    void PrintUsage()
    {
        std::puts(
            "external-esp (C++)\n"
            "  mcesp [--demo] [--pid N] [--port N] [--java PATH] [--jar PATH] [--no-inject] [--open-config] [--screen] [--gdi] [--hz N] [--list] [--record FILE]\n"
            "\n"
            "  --demo       synthetic players (test the overlay/config UI without Minecraft)\n"
            "  --pid N      attach to this java(w).exe PID (two Minecraft clients on one PC)\n"
            "  --port N     UDP port for the agent stream (default 47615)\n"
            "  --java PATH  java.exe of a JDK to run the attach helper (default: the game's own JRE, JAVA_HOME, PATH)\n"
            "  --jar PATH   agent jar (default: esp-agent.jar next to the exe)\n"
            "  --no-inject  do not attach automatically (start the game with -javaagent:esp-agent.jar=port=47615)\n"
            "  --open-config  open the config window right away\n"
            "  --screen     cover the whole primary monitor instead of tracking the game window\n"
            "  --hz N       agent snapshot rate, 1-240 (default 120; more = smoother boxes, a little more CPU in the game)\n"
            "  --gdi        use the legacy CPU renderer (layered window) instead of the GPU swap chain\n"
            "  --record F   also save the raw agent datagrams + arrival times to F (.esprec) for --replay / tests / bug reports\n"
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
    FriendList::Load();
    ProfileManager::Refresh();
    LOG_INFO("Mode: %s | UDP port %u | agent jar: %s", opt.demo ? "DEMO" : "agent", static_cast<unsigned>(opt.port),
             opt.jarPath.string().c_str());

    EspOverlay overlay(opt);
    return overlay.Run();
}
