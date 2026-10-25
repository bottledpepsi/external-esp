// Session token (wire v5): generation, the K line, StreamIngest::Authenticate, replay bypass, the cross-language v5 datagram.
#include "EspRec.h"
#include "Replay.h"
#include "StreamIngest.h"
#include "Logger.h"
#include "Token.h"
#include "Wire.h"
#include "wire_v5_sample.h"
#include <cstdio>
#include <fstream>
#include <set>
#include <string>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

using Clock = StreamIngest::Clock;
using std::chrono::seconds;
static const std::string kTok = "0123456789abcdef0123456789abcdef";

static std::string V5(const std::string& tokenLine) { return "MCRADAR 5\n" + tokenLine + "S ok\nL A 1 2 3 4 5 6 0.6 1.8 u\n"; }

int main()
{
    // ---- generation ----
    {
        std::set<std::string> seen;
        for (int i = 0; i < 200; ++i)
        {
            const std::string t = token::Generate();
            CHECK(t.size() == token::kHexLength && token::IsValid(t));
            for (char c : t) CHECK((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
            seen.insert(t);
        }
        CHECK(seen.size() == 200);                                      // 128 random bits: no repeats
        CHECK(!token::IsValid("") && !token::IsValid("short") && !token::IsValid(std::string(65, 'a')) && !token::IsValid("0123456789abcde!") && !token::IsValid("0123456789abcde ") && token::IsValid(std::string(16, 'a')) && token::IsValid(std::string(64, 'Z')));
        CHECK(token::Equals("abc", "abc") && !token::Equals("abc", "abd") && !token::Equals("abc", "ab") && !token::Equals("", "a") && token::Equals("", ""));
    }

    // ---- the K line in the parser ----
    {
        WorldSnapshot s;
        const std::string d = V5("K " + kTok + "\n");
        CHECK(ParseDatagram(d.data(), d.size(), s) && s.token == kTok && s.protocolVersion == 5 && s.localValid);
        const std::string none = V5("");
        WorldSnapshot n;
        CHECK(ParseDatagram(none.data(), none.size(), n) && n.token.empty() && n.protocolVersion == 5);
        // K is only a token in v5; in v4 it is an unknown line
        const std::string v4 = "MCRADAR 4\nK " + kTok + "\nS ok\n";
        WorldSnapshot v;
        CHECK(ParseDatagram(v4.data(), v4.size(), v) && v.token.empty());
        // malformed tokens: dropped and counted, the datagram itself is still parsed
        for (const char* bad : {"K \n", "K\n", "K a b\n"})
        {
            const std::string d2 = V5(bad);
            WorldSnapshot b;
            CHECK(ParseDatagram(d2.data(), d2.size(), b) && (std::string(bad) == "K a b\n" ? b.token == "a" : b.token.empty()));
        }
        const std::string weird = V5("K bad!token\n"), longTok = V5("K " + std::string(65, 'a') + "\n");
        WorldSnapshot w1, w2;
        CHECK(ParseDatagram(weird.data(), weird.size(), w1) && w1.token.empty() && w1.ignoredLines == 1);
        CHECK(ParseDatagram(longTok.data(), longTok.size(), w2) && w2.token.empty() && w2.ignoredLines == 1);
        // the Java encoder's v5 datagram for the full sample (tests/wire_v5_sample.h; the Java test checks the encoder still writes exactly this)
        const std::string j = kV5Full;
        WorldSnapshot js;
        CHECK(ParseDatagram(j.data(), j.size(), js) && js.token == kV5FullToken && js.protocolVersion == 5 && js.hasSampleTime && js.items.size() == 2 && js.local.name == "Alice");
    }

    // ---- the ingest check ----
    {
        Clock::time_point t = Clock::time_point(seconds(100));
        StreamIngest in([&] { return t; });
        in.SetAuth(true, kTok);
        const std::string good = V5("K " + kTok + "\n"), wrong = V5("K " + std::string(32, 'f') + "\n"), none = V5(""), v4 = "MCRADAR 4\nS ok\nL A 1 2 3\n";
        CHECK(in.AuthRequired() && in.OnDatagram(good.data(), good.size()) && in.PacketCount() == 1);
        CHECK(!in.OnDatagram(wrong.data(), wrong.size()) && !in.OnDatagram(none.data(), none.size()) && !in.OnDatagram(v4.data(), v4.size()));
        CHECK(in.PacketCount() == 1 && in.AuthFailures() == 3 && in.RejectedCount() == 3);
        CHECK(!in.OnDatagram(good.data(), 0));                                       // an empty datagram is not an auth failure
        // a near-miss (one character off, and a prefix) is wrong
        std::string off = kTok;
        off[31] = 'e';
        const std::string offD = V5("K " + off + "\n"), prefix = V5("K " + kTok.substr(0, 31) + "\n"), longer = V5("K " + kTok + "0\n");
        CHECK(!in.OnDatagram(offD.data(), offD.size()) && !in.OnDatagram(prefix.data(), prefix.size()) && !in.OnDatagram(longer.data(), longer.size()));
        CHECK(in.PacketCount() == 1);
        // unauthenticated datagrams never reach Latest()
        CHECK(in.Latest()->seq == 1);
        // HUD message: a fresh good datagram suppresses it; only bad ones for 3 s produce it
        t += seconds(10);
        in.OnDatagram(none.data(), none.size());
        const std::string m1 = in.ProtocolProblem(t);
        CHECK(m1.find("no session token") != std::string::npos && m1.find("--no-auth") != std::string::npos && m1.find("v5") != std::string::npos);
        in.OnDatagram(v4.data(), v4.size());
        CHECK(in.ProtocolProblem(t).find("wire v4") != std::string::npos);
        in.OnDatagram(wrong.data(), wrong.size());
        CHECK(in.ProtocolProblem(t).find("wrong session token") != std::string::npos);
        in.OnDatagram(good.data(), good.size());
        CHECK(in.ProtocolProblem(t).empty());                                        // a readable, authenticated agent is talking
    }

    // ---- the log is not flooded: one line per (reason, version), however the failures interleave ----
    {
        elog::Init(); // start esp_log.txt (this test counts its lines)
        auto readLog = [] { std::ifstream lf("esp_log.txt"); return std::string((std::istreambuf_iterator<char>(lf)), std::istreambuf_iterator<char>()); };
        auto count = [](const std::string& log, const char* needle) { int n = 0; for (size_t p = log.find(needle); p != std::string::npos; p = log.find(needle, p + 1)) ++n; return n; };
        const std::string before = readLog();                                        // earlier blocks of this test have logged their own warnings
        StreamIngest in;
        in.SetAuth(true, kTok);
        const std::string good = V5("K " + kTok + "\n"), wrong = V5("K " + std::string(32, 'f') + "\n"), none = V5(""), v4 = "MCRADAR 4\nS ok\n", f7 = "MCRADAR 7\n", f9 = "MCRADAR 9\n";
        for (int i = 0; i < 500; ++i)
            for (const std::string* d : {&good, &wrong, &none, &v4, &f7, &f9, &wrong, &none})
                in.OnDatagram(d->data(), d->size());
        const std::string after = readLog();
        auto added = [&](const char* needle) { return count(after, needle) - count(before, needle); };
        CHECK(added("WRONG session token") == 1);                                   // 1000 wrong-token datagrams
        CHECK(added("without a session token (wire v5)") == 1 && added("without a session token (wire v4)") == 1);
        CHECK(added("Agent speaks wire protocol v7") == 1 && added("Agent speaks wire protocol v9") == 1);
        CHECK(in.PacketCount() == 500 && in.AuthFailures() == 500 * 5 && in.RejectedCount() == 500 * 7);
    }

    // ---- not required: everything is accepted (replay, tests, --no-auth) ----
    {
        StreamIngest in;
        const std::string wrong = V5("K " + std::string(32, 'f') + "\n"), none = V5(""), v4 = "MCRADAR 4\nS ok\n";
        CHECK(!in.AuthRequired() && in.OnDatagram(wrong.data(), wrong.size()) && in.OnDatagram(none.data(), none.size()) && in.OnDatagram(v4.data(), v4.size()) && in.AuthFailures() == 0);
    }

    // ---- replay is not subject to the check (a recording was authenticated when it was made) ----
    {
        std::ofstream f("tok.esprec", std::ios::binary);
        esprec::Writer w(f);
        w.WriteHeader(esprec::Header{});
        const std::string a = V5("K " + std::string(32, 'a') + "\n"), b = V5("");
        w.Append(0, a.data(), a.size());
        w.Append(8000000, b.data(), b.size());
        w.Flush();
        f.close();
        ReplayPlayer rp;
        CHECK(rp.Load("tok.esprec"));
        StreamIngest in;
        in.SetAuth(true, kTok);                                                      // even if the target ingest requires a token
        rp.Pump(in, Clock::time_point(seconds(5)));
        rp.Pump(in, Clock::time_point(seconds(5)) + std::chrono::milliseconds(20));
        CHECK(in.PacketCount() == 2 && in.AuthFailures() == 0);
    }

    std::printf(fails ? "\n%d FAILURES\n" : "\nALL OK\n", fails);
    return fails;
}
