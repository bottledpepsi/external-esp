#include "SetupApp.h"
#include "Clipboard.h"
#include "ConfigStore.h"
#include "DiagnosticsApp.h"
#include "ExitCodes.h"
#include "Logger.h"
#include "Paths.h"
#include "Platform.h"
#include "ProcessManager.h"
#include "SelfTestApp.h"
#include "SetupChecks.h"
#include "UdpListener.h"
#include "Version.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

bool StdinIsConsole()
{
    DWORD mode = 0;
    return GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) != 0;
}

namespace
{
    bool EnableColor()
    {
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (!GetConsoleMode(h, &mode)) return false; // redirected to a file: no escape codes
        return SetConsoleMode(h, mode | 0x0004 /*ENABLE_VIRTUAL_TERMINAL_PROCESSING*/) != 0;
    }

    bool Ask(const char* question, bool defaultYes)
    {
        std::printf("%s %s ", question, defaultYes ? "[Y/n]" : "[y/N]");
        std::fflush(stdout);
        std::string line;
        if (!std::getline(std::cin, line)) return false;
        for (char& c : line) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (line.empty() || line == "\r") return defaultYes;
        return line[0] == 'y';
    }

    setup::JavaInfo InspectJava(const fs::path& javaExe)
    {
        std::ifstream f(javaExe.parent_path().parent_path() / "release");
        std::stringstream ss;
        ss << f.rdbuf();
        setup::JavaInfo j = f ? setup::ParseRelease(ss.str()) : setup::JavaInfo{};
        j.found = true;
        j.path = javaExe.string();
        return j;
    }

    setup::Facts Gather(const AppOptions& opt)
    {
        setup::Facts f;
        std::error_code ec;

        f.configDir = ConfigDir().string();
        {
            fs::create_directories(ConfigDir(), ec);
            std::string err;
            configstore::WriteOptions o;
            o.keepBackup = false;
            const fs::path probe = ConfigDir() / ".write-test";
            f.configWritable = configstore::WriteAtomic(probe, "ok\n", &err, o);
            fs::remove(probe, ec);
        }

        f.jarOk = fs::exists(opt.jarPath, ec);
        f.jarSource = opt.jarSource.empty() ? opt.jarPath.string() : opt.jarSource;

        f.port = opt.port;
        f.portError = UdpListener::Probe(opt.port);

        // Java for the attach helper: --java, JAVA_HOME, PATH (the game's own is tried first at attach time and is judged by the attach itself)
        std::vector<fs::path> cands;
        if (!opt.javaPath.empty()) cands.push_back(opt.javaPath);
        wchar_t home[MAX_PATH] = {};
        const DWORD n = GetEnvironmentVariableW(L"JAVA_HOME", home, MAX_PATH);
        if (n > 0 && n < MAX_PATH) cands.push_back(fs::path(home) / "bin" / "java.exe");
        wchar_t onPath[MAX_PATH] = {};
        if (SearchPathW(nullptr, L"java.exe", nullptr, MAX_PATH, onPath, nullptr) > 0) cands.push_back(onPath);
        for (const fs::path& c : cands)
        {
            if (!fs::exists(c, ec)) continue;
            const setup::JavaInfo j = InspectJava(c);
            if (!f.java.found) f.java = j; // the first one that exists is the fallback
            if (j.attach != setup::JavaInfo::Attach::No && (j.major == 0 || j.major >= 8))
            {
                f.java = j; // the first one that can do the job wins
                break;
            }
        }

        const auto all = ProcessManager::Scan();
        const auto cl = ProcessManager::Candidates(all);
        f.clients = static_cast<int>(cl.size());
        if (f.clients == 1)
            for (const JavaProcess& p : all)
                if (p.hwnd) { f.clientName = p.DisplayName(); break; }
        return f;
    }

    void OfferDiagnostics(const AppOptions& opt)
    {
        if (!Ask("Copy a diagnostics report (redacted) to the clipboard, to paste into a bug report?", true)) return;
        diag::AppContext ctx;
        ctx.version = kEspVersion;
        ctx.agentLog = opt.exportDir / "esp_agent.log";
        ctx.settings = "port = " + std::to_string(opt.port) + "\nagent hz = " + std::to_string(opt.agentHz) + "\ntoken check = " + (opt.requireAuth ? "on" : "off") + "\n";
        if (clipboard::SetText(diag::Collect(ctx))) std::printf("Copied. It has your user name, folders and player names removed - read it before posting.\n");
        else std::printf("The clipboard could not be opened; write it to a file instead:  mcesp --diagnostics report.txt\n");
    }
} // namespace

int RunSetup(const AppOptions& opt, bool interactive)
{
    std::printf("\nmcesp %s - checking this PC\n\n", kEspVersion);
    const std::vector<setup::Item> items = setup::Evaluate(Gather(opt));
    std::fputs(setup::Render(items, EnableColor()).c_str(), stdout);
    for (const setup::Item& i : items)
        if (i.status != setup::Status::Ok) LOG_INFO("Setup check: %s - %s%s", i.name.c_str(), i.status == setup::Status::Fail ? "FAIL: " : "warning: ", i.detail.c_str());

    const bool failed = setup::HasFailure(items);
    if (interactive)
    {
        if (failed) OfferDiagnostics(opt);
        else
        {
            bool oneClient = false;
            for (const setup::Item& i : items) if (i.name == "Minecraft" && i.status == setup::Status::Ok) oneClient = true;
            if (oneClient && Ask("\nRun the self-test now? (about 25 s: be in a world, look around and drop an item)", false))
            {
                const int code = RunSelfTest(opt); // the same test as --selftest (docs/selftest.md); it prints its own report
                if (code >= 10) OfferDiagnostics(opt);
            }
        }
    }
    return failed ? exitcode::kRuntime : exitcode::kOk;
}
