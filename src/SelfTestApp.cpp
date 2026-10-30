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

    // 1. Which client? Exactly one, chosen by the same rules as the overlay (ProcessSelect.h), or a clear refusal.
    const auto all = ProcessManager::Scan();
    const auto cands = ProcessManager::Candidates(all);
    const procselect::Selector sel{opt.pid, opt.titleSelector, opt.exeSelector};
    const procselect::Result pick = procselect::Select(cands, sel);
    selftest::OverlayFacts facts;
    facts.candidates = pick.idx.size();
    if (pick.kind != procselect::Result::Kind::One)
    {
        std::printf("%s\n", procselect::Explain(cands, sel, pick).c_str());
        const int code = selftest::Decide(facts);
        std::printf("RESULT %d %s\n", code, selftest::Describe(code));
        return code;
    }
    const JavaProcess target = all[pick.idx.front()];
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
