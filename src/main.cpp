// external-esp (C++ port) - external Direct2D overlay for a LOCAL Minecraft Java world.
//
//   Minecraft JVM <--(JVM Attach API)-- esp-agent.jar --UDP 127.0.0.1:47615--> UdpListener --> EspOverlay (D2D layered window)
//
// Usage:
//   mcesp [--demo] [--pid N] [--port N] [--java PATH] [--jar PATH] [--no-inject] [--open-config] [--screen] [--gdi] [--hz N] [--agent-pacer sleep|hybrid] [--pacing-report [S]] [--title T] [--exe E] [--selftest [S]] [--dump-structure] [--list] [--record FILE]
//   Insert = config window, Delete = toggle ESP, hold End = exit (all rebindable in the HOTKEYS tab).
//
// Scope: written for single-player / LAN worlds you control (two accounts on your own machine) -
// reverse-engineering practice, not for use on other people's servers.
#include "AgentInjector.h"
#include "App.h"
#include "DpiInfo.h"
#include "SelfTestApp.h"
#include "EspOverlay.h"
#include "FriendList.h"
#include "Logger.h"
#include "DiagnosticsApp.h"
#include "Paths.h"
#include "Version.h"
#include "Token.h"
#include "Wire.h"
#include "ProcessManager.h"
#include "ProfileManager.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <cstdlib>
#include <fstream>
#include <string>

namespace
{
    void PrintUsage()
    {
        std::puts(
            "external-esp (C++)\n"
            "  mcesp [--demo] [--pid N] [--port N] [--java PATH] [--jar PATH] [--no-inject] [--open-config] [--screen] [--gdi] [--hz N] [--agent-pacer sleep|hybrid] [--pacing-report [S]] [--list] [--record FILE] [--replay FILE [--replay-speed X] [--replay-loop]] [--token T | --no-auth] [--verbose] [--diagnostics [FILE]]\n"
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
            "  --agent-pacer M  how the agent waits for its next tick: sleep (default) or hybrid (sleep + spin the last ~2 ms; precise, costs CPU in the game)\n"
            "  --pacing-report [S]  the agent writes esp_pacing.txt (achieved tick intervals) after S seconds (default 20); see docs/pacing.md\n"
            "  --title T    pick the Minecraft client whose window title contains T (several clients, or a custom client whose title is not \"Minecraft...\")\n"
            "  --exe E      ... whose program name contains E (javaw.exe, java.exe); combines with --title; --pid N names the process directly (docs/launchers.md)\n"
            "  --selftest [S]  attach, watch S seconds (default 20: look around, drop an item), print what works and exit with a code (docs/selftest.md)\n"
            "  --adapter X  GPU for the overlay: its number or part of its name (default: automatic; the log lists every adapter and says why one was chosen)\n"
            "  --simulate-device-loss[=persistent]  test aid: F10 injects a GPU device loss (persistent: recovery fails too, so the GDI fallback must follow)\n"
            "  --dump-structure  the agent also writes esp_structure.json (class structure only, for bug reports; docs/fixtures.md)\n"
            "  --gdi        use the legacy CPU renderer (layered window) instead of the GPU swap chain\n"
            "  --record F   also save the raw agent datagrams + arrival times to F (.esprec) for --replay / tests / bug reports\n"
            "  --replay F   play back an .esprec recording over the whole screen instead of attaching to Minecraft\n"
            "               (F6 pause/resume, F7 step while paused, F8 slower, F9 faster); --replay-speed X, --replay-loop\n"
            "  --diagnostics [F]  print (or write to F) a REDACTED report for bug reports: versions, settings, log tails; then exit\n"
            "  --verbose    also log DEBUG lines (the log is esp_log.txt, capped at 1 MiB + one backup esp_log.1.txt)\n"
            "  --token T    use this session token instead of a random one (16-64 letters/digits; for --no-inject: pass the same to the agent)\n"
            "  --no-auth    accept datagrams without / with any session token (not recommended)\n"
            "  --list       list running Minecraft/java processes and exit\n"
            "\nKeys: Insert config | Delete toggle | hold End exit");
    }
} // namespace

int main(int argc, char** argv)
{
    const std::string dpiLevel = dpi::Enable(); // overlay pixels must equal game-window pixels, on every monitor whatever its scale (logged once the logger is up)
    elog::Init();

    AppOptions opt;
    opt.jarPath = AppDir() / "esp-agent.jar";
    bool list = false, diagnostics = false;
    std::string diagnosticsFile;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--help" || a == "-h")
        {
            PrintUsage();
            return 0;
        }
        else if (a == "--verbose" || a == "-v")
        {
            elog::SetLevel(elog::Level::Debug);
            opt.verbose = true;
        }
        else if (a == "--diagnostics")
        {
            diagnostics = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') diagnosticsFile = argv[++i];
        }
        else if (a == "--demo") opt.demo = true;
        else if (a == "--no-inject") opt.autoInject = false;
        else if (a == "--list") list = true;
        else if (a == "--open-config") opt.openConfig = true;
        else if (a == "--screen") opt.screenOverlay = true;
        else if (a == "--gdi") opt.legacyGfx = true;
        else if (a == "--hz" && i + 1 < argc) opt.agentHz = static_cast<int>(std::clamp(std::strtol(argv[++i], nullptr, 10), 1L, 240L));
        else if (a == "--agent-pacer" && i + 1 < argc)
        {
            opt.agentPacer = argv[++i];
            if (opt.agentPacer != "sleep" && opt.agentPacer != "hybrid")
            {
                std::fprintf(stderr, "--agent-pacer takes sleep or hybrid\n");
                return 2;
            }
        }
        else if (a == "--pacing-report")
        {
            opt.pacingSeconds = 20;
            if (i + 1 < argc && argv[i + 1][0] != '-') opt.pacingSeconds = static_cast<int>(std::clamp(std::strtol(argv[++i], nullptr, 10), 1L, 3600L));
        }
        else if (a == "--selftest")
        {
            opt.selftestSeconds = 20;
            if (i + 1 < argc && argv[i + 1][0] != '-') opt.selftestSeconds = static_cast<int>(std::clamp(std::strtol(argv[++i], nullptr, 10), 1L, 600L));
        }
        else if (a == "--dump-structure") opt.dumpStructure = true;
        else if (a == "--adapter" && i + 1 < argc) opt.adapter = argv[++i];
        else if (a == "--simulate-device-loss") opt.simulateLoss = 1;
        else if (a == "--simulate-device-loss=persistent") opt.simulateLoss = 2;
        else if (a == "--record" && i + 1 < argc) opt.recordPath = argv[++i];
        else if (a == "--token" && i + 1 < argc) opt.token = argv[++i];
        else if (a == "--no-auth") opt.requireAuth = false;
        else if (a == "--replay" && i + 1 < argc) opt.replayPath = argv[++i];
        else if (a == "--replay-speed" && i + 1 < argc) opt.replaySpeed = std::atof(argv[++i]);
        else if (a == "--replay-loop") opt.replayLoop = true;
        else if (a == "--title" && i + 1 < argc) opt.titleSelector = argv[++i];
        else if (a == "--exe" && i + 1 < argc) opt.exeSelector = argv[++i];
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

    if (diagnostics)
    {
        diag::AppContext ctx;
        ctx.version = kEspVersion;
        ctx.agentLog = opt.jarPath.parent_path() / "esp_agent.log";
        ctx.settings = "port = " + std::to_string(opt.port) + "\nagent hz = " + std::to_string(opt.agentHz) + "\ntoken check = " + (opt.requireAuth ? "on" : "off") + "\n";
        const std::string report = diag::Collect(ctx);
        if (diagnosticsFile.empty()) std::fputs(report.c_str(), stdout);
        else
        {
            std::ofstream f(diagnosticsFile, std::ios::binary | std::ios::trunc);
            f << report;
            if (!f)
            {
                std::fprintf(stderr, "cannot write %s\n", diagnosticsFile.c_str());
                return 1;
            }
            std::printf("wrote %s (redacted - read it before posting)\n", diagnosticsFile.c_str());
        }
        return 0;
    }
    if (opt.selftestSeconds > 0) return RunSelfTest(opt);
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
        std::printf("Start the game with:  -javaagent:esp-agent.jar=port=%d;hz=%d;wire=%d%s%s%s%s\n", static_cast<int>(opt.port), opt.agentHz, wire::kMaxVersion,
                    opt.token.empty() ? "" : ";token=", opt.token.c_str(), opt.verbose ? ";verbose=1" : "", opt.agentExtraOptions().c_str());
    FriendList::Load();
    ProfileManager::Refresh();
    LOG_INFO("Mode: %s | UDP port %u | agent jar: %s", opt.demo ? "DEMO" : "agent", static_cast<unsigned>(opt.port),
             opt.jarPath.string().c_str());
    LOG_INFO("DPI awareness: %s", dpiLevel.c_str());
    LOG_INFO("Monitors: %s", dpi::DescribeMonitors().c_str());

    EspOverlay overlay(opt);
    return overlay.Run();
}
