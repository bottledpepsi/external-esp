// ReplayPlayer with a fake clock: delivery times, pause/step, speed, loop, sample-stamp rewriting, bad files. No threads.
#include "EspRec.h"
#include "Replay.h"
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

using Clock = StreamIngest::Clock;
using std::chrono::nanoseconds;
using std::chrono::milliseconds;

static const Clock::time_point kBase = Clock::time_point(std::chrono::seconds(1000000));
static Clock::time_point At(double ms) { return kBase + nanoseconds(static_cast<int64_t>(ms * 1e6)); }

// datagram with agent stamp T (ns) and a marker name so each one is identifiable
static std::string Dg(int64_t tNs, const std::string& who)
{
    return "MCRADAR 4\nT " + std::to_string(tNs) + "\nS ok\nL " + who + " 1 2 3 10 20 17.5\n";
}

struct Item { double tMs; int64_t stampNs; std::string who; };

// Writes an .esprec with the given (arrival ms, agent stamp, name) records.
static std::string Write(const char* path, const std::vector<Item>& items, bool cut = false)
{
    std::ofstream f(path, std::ios::binary);
    esprec::Writer w(f);
    esprec::Header h;
    h.wireVersion = 4;
    w.WriteHeader(h);
    for (const auto& it : items)
    {
        const std::string d = Dg(it.stampNs, it.who);
        w.Append(static_cast<uint64_t>(it.tMs * 1e6) + 5000000, d.data(), d.size());   // recordings start at an arbitrary t: Load() normalises
    }
    w.Flush();
    f.close();
    if (cut)
    {
        std::ifstream in(path, std::ios::binary);
        std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(all.data(), static_cast<std::streamsize>(all.size() - 7));
    }
    return path;
}

int main()
{
    // 5 datagrams 10 ms apart; agent stamps are 10 ms apart too, with 1 ms transport jitter on the third
    const std::vector<Item> five = {{0, 100000000, "A"}, {10, 110000000, "B"}, {20, 119000000, "C"}, {30, 130000000, "D"}, {40, 140000000, "E"}};

    // ---- load + plain 1x playback ----
    {
        ReplayPlayer rp;
        std::string err;
        CHECK(rp.Load(Write("five.esprec", five), &err) && rp.Count() == 5 && rp.WireVersion() == 4);
        StreamIngest in;
        CHECK(rp.Pump(in, At(0)) == 1);                                       // the first datagram is due at once
        CHECK(in.Latest()->local.name == "A" && in.Latest()->arrived == At(0) && in.Latest()->seq == 1);
        CHECK(rp.Pump(in, At(9.9)) == 0 && in.PacketCount() == 1);
        CHECK(rp.Pump(in, At(10)) == 1 && in.Latest()->local.name == "B" && in.Latest()->arrived == At(10));
        // a stall: the frame loop was late by 25 ms -> three datagrams at once, each stamped with when it WOULD have arrived
        CHECK(rp.Pump(in, At(35)) == 2 && in.Latest()->local.name == "D");
        CHECK(in.PacketCount() == 4 && in.Latest()->seq == 4 && in.Latest()->arrived == At(30));
        CHECK(rp.Pump(in, At(50)) == 1 && rp.Finished() && in.Latest()->local.name == "E" && in.Latest()->arrived == At(40));
        CHECK(rp.Pump(in, At(500)) == 0 && in.PacketCount() == 5);
    }

    // ---- the transport jitter of the recording survives at 1x: (arrival - T') == (arrival - T) up to a constant ----
    {
        ReplayPlayer rp;
        CHECK(rp.Load("five.esprec"));
        StreamIngest in;
        std::vector<int64_t> d;
        for (double t = 0; t <= 45; t += 1)
        {
            rp.Pump(in, At(t));
            if (in.PacketCount() > d.size())
            {
                auto s = in.Latest();
                d.push_back(std::chrono::duration_cast<nanoseconds>(s->arrived.time_since_epoch()).count() - s->sampleNs);
            }
        }
        CHECK(d.size() == 5);
        // recorded: arrival 0,10,20,30,40 ms; stamps 100,110,119,130,140 ms -> offsets 0,0,+1ms,0,0 relative to the first
        CHECK(d[1] == d[0] && d[2] == d[0] + 1000000 && d[3] == d[0] && d[4] == d[0]);
    }

    // ---- pause freezes the playhead, step delivers exactly one, resume does not burst ----
    {
        ReplayPlayer rp;
        CHECK(rp.Load("five.esprec"));
        StreamIngest in;
        rp.Pump(in, At(0));
        rp.Pump(in, At(12));                                                   // A, B delivered
        rp.SetPaused(true);
        CHECK(rp.Pump(in, At(500)) == 0 && in.PacketCount() == 2);            // a long pause: nothing, playhead frozen
        CHECK(rp.Step(in, At(501)) && in.Latest()->local.name == "C" && in.Latest()->arrived == At(501));
        CHECK(rp.Step(in, At(502)) && in.Latest()->local.name == "D");
        rp.SetPaused(false);
        CHECK(rp.Pump(in, At(503)) == 0);                                      // playhead is at D (30 ms); E is due at 40: no burst
        CHECK(rp.Pump(in, At(513)) == 1 && in.Latest()->local.name == "E");
        CHECK(!rp.Step(in, At(514)));                                          // not paused: Step does nothing
    }

    // ---- speed: 2x halves the gaps and the agent stamps are rescaled so the Smoother still sees consistent time ----
    {
        ReplayPlayer rp;
        CHECK(rp.Load("five.esprec"));
        rp.SetSpeed(2.0);
        StreamIngest in;
        rp.Pump(in, At(0));
        CHECK(rp.Pump(in, At(4.9)) == 0 && rp.Pump(in, At(5)) == 1 && in.Latest()->arrived == At(5));
        auto b = in.Latest();
        rp.Pump(in, At(10));                                                   // C is due at 10 ms real time
        auto c = in.Latest();
        CHECK(c->local.name == "C" && c->arrived == At(10));
        // stamps: B->C were 9 ms apart in the recording, 5 ms apart in arrival: replay keeps (arrival - T') = D/2
        const int64_t dB = std::chrono::duration_cast<nanoseconds>(b->arrived.time_since_epoch()).count() - b->sampleNs;
        const int64_t dC = std::chrono::duration_cast<nanoseconds>(c->arrived.time_since_epoch()).count() - c->sampleNs;
        CHECK(dC - dB == 500000);                                              // the 1 ms recorded jitter, halved
        rp.SetSpeed(1000);
        CHECK(rp.Speed() == 20.0);                                             // clamped
        rp.SetSpeed(0);
        CHECK(rp.Speed() == 0.05);
    }

    // ---- loop: the sequence repeats, arrival times and sample stamps keep increasing across the seam ----
    {
        ReplayPlayer rp;
        CHECK(rp.Load("five.esprec"));
        rp.SetLoop(true);
        StreamIngest in;
        std::string names;
        int64_t lastArr = -1, lastStamp = -1;
        bool monotone = true;
        for (double t = 0; t <= 160; t += 1)
        {
            const uint64_t before = in.PacketCount();
            rp.Pump(in, At(t));
            if (in.PacketCount() > before)
            {
                auto s = in.Latest();
                names += s->local.name;
                const int64_t arr = std::chrono::duration_cast<nanoseconds>(s->arrived.time_since_epoch()).count();
                monotone = monotone && arr > lastArr && s->sampleNs > lastStamp;
                lastArr = arr;
                lastStamp = s->sampleNs;
            }
        }
        CHECK(names.size() >= 15 && names.substr(0, 15) == "ABCDEABCDEABCDE");
        CHECK(monotone && !rp.Finished() && rp.Loops() >= 2);
    }

    // ---- an invalid datagram inside a recording is counted, not fatal ----
    {
        std::ofstream f("bad.esprec", std::ios::binary);
        esprec::Writer w(f);
        w.WriteHeader(esprec::Header{});
        const std::string ok = Dg(1, "A");
        w.Append(0, ok.data(), ok.size());
        w.Append(8000000, "this is not a datagram", 22);
        w.Append(16000000, ok.data(), ok.size());
        w.Flush();
        f.close();
        ReplayPlayer rp;
        CHECK(rp.Load("bad.esprec"));
        StreamIngest in;
        rp.Pump(in, At(0));
        rp.Pump(in, At(100));
        CHECK(in.PacketCount() == 2 && in.RejectedCount() == 1 && rp.Finished());
    }

    // ---- files that cannot be used ----
    {
        ReplayPlayer rp;
        std::string err;
        CHECK(!rp.Load("does-not-exist.esprec", &err) && !err.empty());
        { std::ofstream f("junk.esprec", std::ios::binary); f << "MCRADAR 4\nS ok\n"; }
        err.clear();
        CHECK(!rp.Load("junk.esprec", &err) && err.find("magic") != std::string::npos);
        Write("empty.esprec", {});
        err.clear();
        CHECK(!rp.Load("empty.esprec", &err) && err.find("no datagrams") != std::string::npos);
        // a recording that was cut off loads up to the last complete record
        Write("cut.esprec", five, true);
        ReplayPlayer rc;
        CHECK(rc.Load("cut.esprec") && rc.Truncated() && rc.Count() == 4);
    }

    // ---- deterministic: the same recording and the same fake times give the same output ----
    {
        auto run = [&]
        {
            ReplayPlayer rp;
            rp.Load("five.esprec");
            rp.SetLoop(true);
            StreamIngest in;
            std::string log;
            for (double t = 0; t < 130; t += 3.3)
            {
                rp.Pump(in, At(t));
                if (auto s = in.Latest()) log += s->local.name + std::to_string(s->seq) + "@" + std::to_string(s->sampleNs) + ";";
            }
            return log;
        };
        CHECK(run() == run());
    }

    // ---- the real-clock driver thread: plays a 40 ms recording to the end, pauses, steps, changes speed ----
    {
        ReplayPlayer rp;
        CHECK(rp.Load("five.esprec"));
        StreamIngest in;
        {
            ReplayDriver drv(rp, in);
            drv.Start();
            std::this_thread::sleep_for(milliseconds(300));
            CHECK(drv.Finished() && in.PacketCount() == 5 && drv.Position() == 5);
        }
        ReplayPlayer lp;
        CHECK(lp.Load("five.esprec"));
        lp.SetLoop(true);
        StreamIngest in2;
        ReplayDriver d2(lp, in2);
        d2.Start();
        std::this_thread::sleep_for(milliseconds(150));
        CHECK(in2.PacketCount() > 5);                                          // looping
        d2.TogglePause();
        std::this_thread::sleep_for(milliseconds(50));
        const uint64_t frozen = in2.PacketCount();
        std::this_thread::sleep_for(milliseconds(100));
        CHECK(d2.Paused() && in2.PacketCount() == frozen);
        d2.RequestStep();
        std::this_thread::sleep_for(milliseconds(50));
        CHECK(in2.PacketCount() == frozen + 1);
        d2.SetSpeed(4.0);
        std::this_thread::sleep_for(milliseconds(50));
        CHECK(d2.Speed() == 4.0);
        d2.Stop();
    }

    std::printf(fails ? "\n%d FAILURES\n" : "\nALL OK\n", fails);
    return fails;
}
