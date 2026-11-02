// external-esp (C++ port) - external Direct2D overlay for a LOCAL Minecraft Java world.
//
//   Minecraft JVM <--(JVM Attach API)-- esp-agent.jar --UDP 127.0.0.1:47615--> UdpListener --> EspOverlay (D2D layered window)
//
// Usage:
//   mcesp [--demo] [--pid N] [--port N] [--java PATH] [--jar PATH] [--no-inject] [--open-config] [--profile NAME] [--screen] [--gdi] [--hz N] [--agent-pacer sleep|hybrid] [--pacing-report [S]] [--title T] [--exe E] [--selftest [S]] [--dump-structure] [--list] [--record FILE]
//   Insert = config window, Delete = toggle ESP, hold End = exit (all rebindable in the HOTKEYS tab).
//
// Scope: written for single-player / LAN worlds you control (two accounts on your own machine) -
// reverse-engineering practice, not for use on other people's servers.
#include "AgentInjector.h"
#include "App.h"
#include "DpiInfo.h"
#include "PacingTestApp.h"
#include "SelfTestApp.h"
#include "EspOverlay.h"
#include "FriendList.h"
#include "AgentJarWin.h"
#include "CliSpec.h"
#include "ExitCodes.h"
#include "LegacyImport.h"
#include "Shutdown.h"
#include "SetupApp.h"
#include "SingleInstance.h"
#include "UdpListener.h"
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
    // Ctrl+C, Ctrl+Break, closing the console window, log-off, shutdown: ask the overlay loop to leave (it pauses the agent, saves, releases the keys)
    // and, for the events after which Windows kills the process in ~5 s, wait for it to finish. A second Ctrl+C while shutting down ends it at once.
    BOOL WINAPI ConsoleCtrl(DWORD type)
    {
        using exitflow::Reason;
        switch (type)
        {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
            if (!exitflow::Global().Request(Reason::CtrlC)) ExitProcess(130); // already shutting down and the user insists
            return TRUE;
        case CTRL_CLOSE_EVENT:
            exitflow::Global().Request(Reason::ConsoleClosed);
            exitflow::Global().WaitDone(std::chrono::milliseconds(4500));
            return TRUE;
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            exitflow::Global().Request(Reason::LogOff);
            exitflow::Global().WaitDone(std::chrono::milliseconds(4500));
            return TRUE;
        default: return FALSE;
        }
    }

    // "the checklist ran and passed": it is not offered again on its own
    void MarkSetupDone()
    {
        std::error_code ec;
        std::filesystem::create_directories(ConfigDir(), ec);
        if (std::ofstream f(ConfigDir() / ".setup-done"); f) f << "The first-run checklist passed once. Delete this file to see it again at the next start (or run mcesp --setup).\n";
    }

    void PrintUsage() { std::fputs(cli::Help().c_str(), stdout); }
} // namespace

int main(int argc, char** argv)
{
    const std::string dpiLevel = dpi::Enable(); // overlay pixels must equal game-window pixels, on every monitor whatever its scale (logged once the logger is up)
    elog::Init();

    // the whole command line is checked first, against the table in CliSpec.cpp: a typo or a bad number stops here with a sentence, not later with a log line
    const cli::Check pre = cli::Preflight(argc, argv);
    if (pre.result == cli::Check::Result::Help)
    {
        PrintUsage();
        return exitcode::kOk;
    }
    if (pre.result == cli::Check::Result::Error)
    {
        std::fprintf(stderr, "%s\nTry:  mcesp --help\n", pre.message.c_str());
        return exitcode::kUsage;
    }

    AppOptions opt;
    std::filesystem::path explicitJar; // --jar
    bool list = false, diagnostics = false;
    int pacingTest = 0; // --pacing-test [S]
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
        else if (a == "--setup") opt.setupOnly = true;
        else if (a == "--open-config") opt.openConfig = true;
        else if (a == "--screen") opt.screenOverlay = true;
        else if (a == "--gdi") opt.legacyGfx = true;
        else if (a == "--profile" && i + 1 < argc) opt.profile = argv[++i];
        else if (a == "--hz" && i + 1 < argc) {
            opt.agentHz = static_cast<int>(std::clamp(std::strtol(argv[++i], nullptr, 10), 1L, 240L));
            opt.agentHzFromCli = true;
        }
        else if (a == "--agent-pacer" && i + 1 < argc)
        {
            opt.agentPacer = argv[++i];
            if (opt.agentPacer != "sleep" && opt.agentPacer != "hybrid")
            {
                std::fprintf(stderr, "--agent-pacer takes sleep or hybrid\n");
                return exitcode::kUsage;
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
        else if (a == "--pacing-test")
        {
            pacingTest = 2;
            if (i + 1 < argc && argv[i + 1][0] != '-') pacingTest = static_cast<int>(std::clamp(std::strtol(argv[++i], nullptr, 10), 1L, 60L));
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
        else if (a == "--jar" && i + 1 < argc) explicitJar = argv[++i];
        else
        {
            PrintUsage(); // unreachable while the table and this list agree (tests/cli_spec_test.cpp)
            return exitcode::kUsage;
        }
    }

    // settings live in %APPDATA%\external-esp; the first start after an update copies what older builds kept next to the exe
    legacy::ImportFromExeFolder(AppDir(), ConfigDir());
    LOG_INFO("Config folder: %s", ConfigDir().string().c_str());
    // the very first start (no settings yet, the checklist not offered yet): run it once, if there is a console to talk to
    const bool firstRun = [&]
    {
        std::error_code ec;
        return !std::filesystem::exists(ConfigDir() / "esp_config.json", ec) && !std::filesystem::exists(ConfigDir() / ".setup-done", ec);
    }();
    // which agent jar: --jar, else the one inside this exe (extracted and verified), else esp-agent.jar next to it
    {
        const agentjar::Resolved j = agentjar::Resolve(explicitJar, AppDir() / "esp-agent.jar");
        opt.jarPath = j.jar;
        opt.exportDir = j.exportDir;
        using S = agentjar::Resolved::Source;
        opt.jarSource = j.source == S::Embedded ? "embedded in this exe" : j.source == S::CommandLine ? "--jar " + j.jar.string() : "esp-agent.jar next to the exe";
        if (j.source == S::Embedded)
            LOG_INFO("Agent jar: embedded in this exe (sha256 %s) -> %s%s", j.hash.substr(0, 12).c_str(), j.jar.string().c_str(),
                     j.how == agentjar::Extracted::How::Created ? " (extracted now)" : j.how == agentjar::Extracted::How::Repaired ? " (rewritten: the copy was damaged)" : "");
        else if (j.source == S::CommandLine) LOG_INFO("Agent jar: %s (--jar)", j.jar.string().c_str());
        else LOG_INFO("Agent jar: %s (next to the exe; this build has no embedded jar%s)", j.jar.string().c_str(), j.problem.empty() ? "" : " that could be used");
    }

    // a mode that attaches or starts the agent needs the jar: say so now, with what to do, instead of a failed injection later
    if (opt.setupOnly)
    {
        const int rc = RunSetup(opt, StdinIsConsole());
        if (rc == exitcode::kOk) MarkSetupDone();
        return rc;
    }
    if (firstRun && !list && !diagnostics && pacingTest == 0 && opt.selftestSeconds == 0 && !opt.demo && opt.replayPath.empty() && StdinIsConsole())
    {
        std::printf("First start: a quick check of this PC (it runs once; run it again any time with  mcesp --setup).\n");
        if (RunSetup(opt, true) != exitcode::kOk)
        {
            std::printf("\nFix what is marked [FAIL] and start again - this check will run again until it passes.\n");
            return exitcode::kRuntime;
        }
        MarkSetupDone();
    }
    if (!list && !diagnostics && pacingTest == 0 && !opt.demo && opt.replayPath.empty())
    {
        std::error_code jec;
        if (!std::filesystem::exists(opt.jarPath, jec))
        {
            std::printf("There is no agent jar: %s\n"
                        "  This exe has none embedded and there is no esp-agent.jar next to it.\n"
                        "  Build the agent (agent\\build.bat, needs a JDK) and then the exe again, or pass  --jar <path to esp-agent.jar>.\n",
                        opt.jarPath.string().c_str());
            LOG_ERROR("No agent jar at %s", opt.jarPath.string().c_str());
            return exitcode::kNoAgentJar;
        }
    }

    if (diagnostics)
    {
        diag::AppContext ctx;
        ctx.version = kEspVersion;
        ctx.agentLog = opt.exportDir / "esp_agent.log";
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
    if (pacingTest > 0) return RunPacingTest(opt, pacingTest);
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
    if (!opt.agentHzFromCli)
        if (const int hz = Settings::StoredAgentHz(); hz > 0) opt.agentHz = hz; // the ADVANCED tab's setting; --hz overrides it
    if (!opt.autoInject && !opt.demo && opt.replayPath.empty())
        std::printf("Start the game with:  -javaagent:%s=port=%d;hz=%d;wire=%d%s%s%s%s\n", opt.jarPath.string().c_str(), static_cast<int>(opt.port), opt.agentHz, wire::kMaxVersion,
                    opt.token.empty() ? "" : ";token=", opt.token.c_str(), opt.verbose ? ";verbose=1" : "", opt.agentExtraOptions().c_str());
    FriendList::Load();
    ProfileManager::Refresh();
    ProfileManager::SeedBuiltIns(); // default / testing / low-power, once
    if (!opt.profile.empty())
    {
        const std::string found = ProfileManager::Find(opt.profile);
        if (found.empty())
        {
            std::string all;
            for (const std::string& n : ProfileManager::ProfileNames()) all += (all.empty() ? "" : ", ") + n;
            std::printf("There is no profile named '%s'. Profiles: %s\n", opt.profile.c_str(), all.empty() ? "(none)" : all.c_str());
            LOG_ERROR("--profile '%s' does not exist.", opt.profile.c_str());
            return exitcode::kUsage;
        }
        opt.profile = found;
    }
    LOG_INFO("Mode: %s | UDP port %u | agent jar: %s", opt.demo ? "DEMO" : "agent", static_cast<unsigned>(opt.port),
             opt.jarPath.string().c_str());
    LOG_INFO("DPI awareness: %s", dpiLevel.c_str());
    LOG_INFO("Monitors: %s", dpi::DescribeMonitors().c_str());

    // one overlay per port: a second one could not receive anything (the port is taken) - say so now, and who has it
    SingleInstance single;
    if (!opt.demo && opt.replayPath.empty() && !single.Acquire(opt.port))
    {
        char who[64] = "";
        if (single.OtherPid()) std::snprintf(who, sizeof who, " (process %lu)", static_cast<unsigned long>(single.OtherPid()));
        std::printf("mcesp is already running%s on port %u.\n"
                    "  - Close it first (hold the exit hotkey, or close its console), or\n"
                    "  - to run a second client on this PC, start this one with  --port <another port> --pid <the game's process id>.\n",
                    who, static_cast<unsigned>(opt.port));
        LOG_ERROR("Another mcesp is already running on port %u%s.", static_cast<unsigned>(opt.port), who);
        return exitcode::kAlreadyRunning;
    }
    if (!opt.demo && opt.replayPath.empty())
        if (const int perr = UdpListener::Probe(opt.port); perr != 0)
        {
            const std::string why = cli::DescribePortError(opt.port, perr);
            std::printf("%s\n", why.c_str());
            LOG_ERROR("%s", why.c_str());
            return exitcode::kPortInUse;
        }
    SetConsoleCtrlHandler(ConsoleCtrl, TRUE); // only for the overlay: the short modes keep the normal Ctrl+C
    EspOverlay overlay(opt);
    const int rc = overlay.Run();
    return rc;
}
