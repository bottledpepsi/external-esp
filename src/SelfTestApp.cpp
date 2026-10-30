#include "SelfTestApp.h"
#include "AgentInjector.h"
#include "Logger.h"
#include "ProcessManager.h"
#include "SelfTestReport.h"
#include "SysInfo.h"
#include "UdpListener.h"
#include "Version.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

using namespace std::chrono_literals;

int RunSelfTest(const AppOptions& opt)
{
    namespace fs = std::filesystem;
    const int seconds = opt.selftestSeconds;
    std::printf("mcesp %s --selftest %d\n", kEspVersion, seconds);

    // 1. Which client? Exactly one, or a clear refusal.
    std::vector<JavaProcess> mc;
    for (const auto& p : ProcessManager::Scan())
    {
        if (opt.pid != 0 ? p.pid == opt.pid : p.title.rfind("Minecraft", 0) == 0) mc.push_back(p);
    }
    selftest::OverlayFacts facts;
    facts.candidates = mc.size();
    if (mc.size() != 1)
    {
        if (mc.empty()) std::puts(opt.pid ? "No java process with that PID. Use --list." : "No Minecraft client found (no java window titled \"Minecraft...\"). Start the game first; --list shows what is running.");
        else
        {
            std::puts("More than one Minecraft client was found; choose one with --pid <PID>:");
            for (const auto& p : mc) std::printf("  PID %lu  %s  window '%s'\n", static_cast<unsigned long>(p.pid), p.DisplayName().c_str(), p.title.c_str());
        }
        std::printf("RESULT %d %s\n", selftest::Decide(facts), selftest::Describe(selftest::Decide(facts)));
        return selftest::Decide(facts);
    }
    const JavaProcess target = mc.front();
    std::printf("target: %s, window '%s'\n", target.DisplayName().c_str(), target.title.c_str());

    // 2. The UDP side: the same listener and token check as the overlay.
    UdpListener udp(opt.port);
    udp.Ingest().SetAuth(opt.requireAuth, opt.token);
    if (!udp.Start())
    {
        facts.udpBound = false;
        std::printf("RESULT %d %s\n", selftest::Decide(facts), selftest::Describe(selftest::Decide(facts)));
        return selftest::Decide(facts);
    }

    // 3. Attach with selftest=SECONDS (AppOptions::agentExtraOptions). The report is written next to the jar.
    const fs::path report = opt.jarPath.parent_path() / "esp_selftest.txt";
    std::error_code ec;
    fs::remove(report, ec);
    AgentInjector inj(InjectorConfig{opt.jarPath, opt.javaPath, opt.port, opt.agentHz, opt.verbose, opt.token, opt.agentExtraOptions()});
    inj.Start(target);
    std::printf("attaching the agent ...\n");
    const auto t0 = std::chrono::steady_clock::now();
    std::string lastMsg;
    while (inj.GetState() == AgentInjector::State::Running || inj.GetState() == AgentInjector::State::Idle)
    {
        if (std::chrono::steady_clock::now() - t0 > 90s) break;
        const std::string m = inj.Message();
        if (m != lastMsg) { std::printf("  %s\n", m.c_str()); lastMsg = m; }
        std::this_thread::sleep_for(200ms);
    }
    if (inj.GetState() != AgentInjector::State::Succeeded)
    {
        facts.attachOk = false;
        std::printf("attach failed: %s\n", inj.Message().c_str());
        const int code = selftest::Decide(facts);
        std::printf("RESULT %d %s\n", code, selftest::Describe(code));
        return code;
    }
    std::printf("agent attached. Self-test running for %d s: look around with the mouse and drop an item (Q) meanwhile ...\n", seconds);

    // 4. Wait for the report (the agent writes it when its window is over).
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds + 25);
    while (std::chrono::steady_clock::now() < deadline && !fs::exists(report, ec)) std::this_thread::sleep_for(250ms);
    facts.reportArrived = fs::exists(report, ec);
    std::string text;
    if (facts.reportArrived)
    {
        std::this_thread::sleep_for(300ms); // the agent may still be closing the file
        std::ifstream f(report, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        text = ss.str();
        facts.agent = selftest::Parse(text);
    }
    facts.packets = udp.PacketCount();
    const uint64_t rejected = udp.Ingest().RejectedCount();
    udp.Stop();

    // 5. One report: the agent's lines, then what only the overlay can see.
    std::puts("");
    if (facts.reportArrived) std::fputs(text.c_str(), stdout);
    else std::printf("no report arrived in %d s. If the agent was already loaded from another folder, its esp_selftest.txt is next to that jar.\n", seconds + 25);
    const sysinfo::Info si = sysinfo::Collect();
    std::printf("overlay: mcesp %s on %s\n", kEspVersion, si.os.c_str());
    if (!target.versionHint.empty()) std::printf("overlay: minecraft per window title: %s\n", target.versionHint.c_str());
    std::printf("overlay: UDP port %d: %llu datagrams accepted, %llu rejected (token check %s)\n", static_cast<int>(opt.port), static_cast<unsigned long long>(facts.packets),
                static_cast<unsigned long long>(rejected), opt.requireAuth ? "on" : "OFF");
    const int code = selftest::Decide(facts);
    std::printf("RESULT %d %s\n", code, selftest::Describe(code));
    return code;
}
