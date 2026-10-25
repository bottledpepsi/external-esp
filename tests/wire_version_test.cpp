// Version handshake: why a datagram was refused (stranger vs. agent of another protocol version) and what the HUD says about it.
#include "EspRec.h"
#include "Replay.h"
#include "StreamIngest.h"
#include "Wire.h"
#include <cstdio>
#include <fstream>
#include <string>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

using Clock = StreamIngest::Clock;
using std::chrono::milliseconds;
using std::chrono::seconds;

static ParseResult Ex(const std::string& d) { WorldSnapshot s; return ParseDatagramEx(d.data(), d.size(), s); }

int main()
{
    // ---- status codes ----
    {
        CHECK(Ex("MCRADAR 4\n").status == ParseStatus::Ok && Ex("MCRADAR 4\n").version == 4);
        for (int v = 1; v <= 5; ++v) CHECK(Ex("MCRADAR " + std::to_string(v) + "\nL A 1 2 3\n").status == ParseStatus::Ok && Ex("MCRADAR " + std::to_string(v) + "\n").version == v);
        CHECK(Ex("MCRADAR 5\n").status == ParseStatus::Ok && Ex("MCRADAR 6\n").status == ParseStatus::UnsupportedVersion && Ex("MCRADAR 6\n").version == 6);
        CHECK(Ex("MCRADAR 10\n").status == ParseStatus::UnsupportedVersion && Ex("MCRADAR 10\n").version == 10);
        CHECK(Ex("MCRADAR 999\n").status == ParseStatus::UnsupportedVersion && Ex("MCRADAR 999\n").version == 999);
        CHECK(Ex("hello").status == ParseStatus::NotOurs && Ex("").status == ParseStatus::NotOurs && Ex("MCRADAR 4junk\n").status == ParseStatus::NotOurs);
        CHECK(Ex("MCRADAR 0\n").status == ParseStatus::NotOurs && Ex("MCRADAR 1000\n").status == ParseStatus::NotOurs && Ex("MCRADAR x\n").status == ParseStatus::NotOurs);
        WorldSnapshot s;
        CHECK(ParseDatagramEx("MCRADAR 3\nL A 1 2 3\n", 20, s).status == ParseStatus::Ok && s.protocolVersion == 3);
        // an unsupported datagram leaves the output alone
        WorldSnapshot keep;
        keep.status = "kept";
        CHECK(ParseDatagramEx("MCRADAR 7\nS new\n", 16, keep).status == ParseStatus::UnsupportedVersion && keep.status == "kept");
        // the old bool API still works
        CHECK(ParseDatagram("MCRADAR 4\n", 10, s) && !ParseDatagram("MCRADAR 6\n", 10, s));
        CHECK(wire::kMinVersion == 1 && wire::kMaxVersion == 5);
    }

    // ---- ingest: what the HUD is told ----
    {
        Clock::time_point t = Clock::time_point(seconds(1000));
        StreamIngest in([&] { return t; });
        CHECK(in.ProtocolProblem(t).empty() && in.UnsupportedVersionSeen() == 0 && in.AcceptedVersion() == 0);

        // a good v4 agent: no problem
        in.OnDatagram("MCRADAR 4\nS ok\n", 15);
        CHECK(in.AcceptedVersion() == 4 && in.ProtocolProblem(t).empty());

        // a stranger on the port: counted, but not a protocol problem
        in.OnDatagram("GET / HTTP/1.1\r\n", 16);
        CHECK(in.RejectedCount() == 1 && in.UnsupportedVersionSeen() == 0 && in.ProtocolProblem(t).empty());

        // a future agent talks while the good one is still fresh: a mixed state is not reported
        t += milliseconds(10);
        in.OnDatagram("MCRADAR 6\nS ok\n", 15);
        CHECK(in.UnsupportedVersionSeen() == 6 && in.RejectedCount() == 2 && in.ProtocolProblem(t).empty());

        // only the future agent from now on: after 3 s without a readable datagram the message appears
        t += seconds(2);
        in.OnDatagram("MCRADAR 6\nS ok\n", 15);
        CHECK(in.ProtocolProblem(t).empty());                                    // the last readable one is 2 s old: still fine
        t += seconds(2);
        in.OnDatagram("MCRADAR 6\nS ok\n", 15);
        const std::string msg = in.ProtocolProblem(t);
        CHECK(msg.find("v6") != std::string::npos && msg.find("v1-v5") != std::string::npos && msg.find("rebuild") != std::string::npos);
        // ... and goes away again when the agent is replaced by a readable one
        in.OnDatagram("MCRADAR 4\nS ok\n", 15);
        CHECK(in.ProtocolProblem(t).empty());
        // ... but is back if only the unsupported one is heard later
        t += seconds(5);
        in.OnDatagram("MCRADAR 7\n", 10);
        CHECK(in.ProtocolProblem(t).find("v7") != std::string::npos);
        // ... and stops being reported when nothing at all is heard any more (the stale-data message covers that)
        t += seconds(10);
        CHECK(in.ProtocolProblem(t).empty());
    }
    {
        // nothing readable ever: the very first unsupported datagram is reported at once
        Clock::time_point t = Clock::time_point(seconds(50));
        StreamIngest in([&] { return t; });
        in.OnDatagram("MCRADAR 8\nS x\n", 14);
        CHECK(in.ProtocolProblem(t).find("v8") != std::string::npos && in.PacketCount() == 0);
    }
    {
        // an old agent (v3) is readable but flagged
        Clock::time_point t = Clock::time_point(seconds(50));
        StreamIngest in([&] { return t; });
        in.OnDatagram("MCRADAR 3\nL A 1 2 3 4 5 6 0.6 1.8 u\n", 36);
        CHECK(in.PacketCount() == 1 && in.AcceptedVersion() == 3);
        const std::string msg = in.ProtocolProblem(t);
        CHECK(msg.find("old wire protocol v3") != std::string::npos && msg.find("esp-agent.jar") != std::string::npos);
        t += seconds(10);                                                        // stale: not this message's business
        CHECK(in.ProtocolProblem(t).empty());
        in.OnDatagram("MCRADAR 4\n", 10);
        CHECK(in.ProtocolProblem(t).empty());                                    // upgraded to v4
    }

    // ---- replay of a recording made by a different build reports the same way ----
    {
        std::ofstream f("v6.esprec", std::ios::binary);
        esprec::Writer w(f);
        w.WriteHeader(esprec::Header{});
        const std::string a = "MCRADAR 6\nS future\n", b = "MCRADAR 4\nS ok\n";
        w.Append(0, a.data(), a.size());
        w.Append(8000000, b.data(), b.size());
        w.Flush();
        f.close();
        ReplayPlayer rp;
        CHECK(rp.Load("v6.esprec"));
        Clock::time_point t = Clock::time_point(seconds(7));
        StreamIngest in([&] { return t; });
        rp.Pump(in, t);
        t += milliseconds(10);
        rp.Pump(in, t);
        CHECK(in.PacketCount() == 1 && in.RejectedCount() == 1 && in.UnsupportedVersionSeen() == 6);
    }

    std::printf(fails ? "\n%d FAILURES\n" : "\nALL OK\n", fails);
    return fails;
}
