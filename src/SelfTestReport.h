// SelfTestReport.h - the agent's self-test report (agent/src/mcradar/SelfTestRun.java) parsed, and the exit code the overlay's --selftest returns.
// Portable (no Windows headers) so tests/ can run it on any platform. The report is line based:
//   esp-selftest 1 / "key: value" header lines / CHECK <name> <PASS|WARN|FAIL|SKIP> <text> / RESULT <word> / EXIT <n> / FILES ...
#pragma once
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace selftest
{
    // Exit codes (docs/selftest.md). 0..3 and 10, 11 come from the agent; the rest are decided here.
    enum Exit : int
    {
        kOk = 0,
        kSkips = 1,
        kWarn = 2,
        kFail = 3,
        kNotMinecraft = 10,
        kNotInWorld = 11,
        kNoData = 12,
        kAmbiguous = 13,
        kAttachFailed = 14,
        kNoReport = 15,
        kPortInUse = 16,
    };

    struct Check
    {
        std::string name, level, text;
    };

    struct Report
    {
        bool valid = false; // starts with "esp-selftest 1" and has an EXIT line
        int exitCode = kNoReport;
        std::string result;
        std::vector<std::string> header; // "agent: ...", "java: ...", "minecraft: ..." and the other key: value lines, in order
        std::vector<Check> checks;
    };

    inline Report Parse(const std::string& text)
    {
        Report r;
        size_t pos = 0;
        bool first = true, haveExit = false;
        while (pos < text.size())
        {
            size_t e = text.find('\n', pos);
            std::string line = text.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
            pos = e == std::string::npos ? text.size() : e + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (first)
            {
                first = false;
                if (line != "esp-selftest 1") return r; // unknown format: not valid
                continue;
            }
            if (line.rfind("CHECK ", 0) == 0)
            {
                size_t a = line.find(' ', 6), b = a == std::string::npos ? a : line.find(' ', a + 1);
                if (a == std::string::npos) continue;
                Check c;
                c.name = line.substr(6, a - 6);
                c.level = b == std::string::npos ? line.substr(a + 1) : line.substr(a + 1, b - a - 1);
                c.text = b == std::string::npos ? "" : line.substr(b + 1);
                r.checks.push_back(std::move(c));
            }
            else if (line.rfind("RESULT ", 0) == 0) r.result = line.substr(7);
            else if (line.rfind("EXIT ", 0) == 0)
            {
                r.exitCode = std::atoi(line.c_str() + 5);
                haveExit = true;
            }
            else if (line.rfind("FILES ", 0) == 0) continue;
            else if (!line.empty() && line.find(": ") != std::string::npos) r.header.push_back(line);
        }
        r.valid = !first && haveExit;
        return r;
    }

    struct OverlayFacts
    {
        size_t candidates = 1; // Minecraft clients found (0 = none, >1 = ambiguous)
        bool udpBound = true;
        bool attachOk = true;
        bool reportArrived = true;
        Report agent;
        uint64_t packets = 0; // valid datagrams the overlay received over UDP during the run
    };

    // What the process returns. Order matters: nothing found / ambiguity / port / attach / no report come before the agent's own verdict, and the agent's
    // "not Minecraft" and "not in a world" are final; for any other verdict the overlay additionally needs data over UDP (otherwise the agent
    // saw the game fine but nothing would reach the overlay: exit 12).
    inline int Decide(const OverlayFacts& f)
    {
        if (f.candidates == 0) return kNotMinecraft;
        if (f.candidates > 1) return kAmbiguous;
        if (!f.udpBound) return kPortInUse;
        if (!f.attachOk) return kAttachFailed;
        if (!f.reportArrived || !f.agent.valid) return kNoReport;
        if (f.agent.exitCode == kNotMinecraft || f.agent.exitCode == kNotInWorld) return f.agent.exitCode;
        if (f.packets == 0) return kNoData;
        return f.agent.exitCode;
    }

    inline const char* Describe(int code)
    {
        switch (code)
        {
        case kOk: return "everything passed";
        case kSkips: return "passed, but some checks could not be exercised (see the SKIP lines)";
        case kWarn: return "works, with warnings";
        case kFail: return "something that should work does not (see the FAIL lines)";
        case kNotMinecraft: return "no Minecraft client was found";
        case kNotInWorld: return "Minecraft is running but no world has been joined yet: join one and repeat";
        case kNoData: return "the agent runs but no data arrived over UDP: check --port, a firewall, or a leftover agent from another session";
        case kAmbiguous: return "more than one Minecraft client was found: choose one with --pid or --title";
        case kAttachFailed: return "the agent could not be attached";
        case kNoReport: return "the agent was attached but no report arrived";
        case kPortInUse: return "the UDP port is in use: another mcesp is running (close it) or pick another --port";
        default: return "unknown";
        }
    }
} // namespace selftest
