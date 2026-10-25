// Concurrency stress test: every thread hand-off of the real program running at once, for ESP_STRESS_SECONDS (default 1.5).
//   UDP thread (producer)  parse -> auth -> sequence -> publish (StreamIngest) + recorder tee (lock-free ring -> writer thread)
//   render thread          Latest() -> Smoother.Push -> PredictInto (what EspOverlay::RenderFrame does)
//   HUD thread             counters, LinkSummary(), ProtocolProblem() (what the status line reads)
//   replay driver          a second ingest fed by ReplayDriver at 20x while another thread toggles pause / step / speed
// Meant to run under ThreadSanitizer (CI does) but its own assertions also catch lost or duplicated datagrams in a plain build.
#include "EspRec.h"
#include "Recorder.h"
#include "Replay.h"
#include "Smoother.h"
#include "StreamIngest.h"
#include "Wire.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#ifndef ESP_TEST_DATA_DIR
#error "ESP_TEST_DATA_DIR must point at tests/data"
#endif

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

using Clock = StreamIngest::Clock;

static const std::string kTok = "0123456789abcdef0123456789abcdef";

static std::string Datagram(uint64_t seq, double t, const std::string& token)
{
    char b[1024];
    std::snprintf(b, sizeof b,
                  "MCRADAR 5\nK %s\nQ 0000beef %llu\nT %lld\nS stress\nC 100.0000 65.6200 -50.0000 %.2f 10.00 70.00 1\nL Me %.3f 64.000 -50.000 %.2f 10.00 20.00 0.60 1.80 u\n"
                  "P Bob %.3f 64.000 -40.000 0.00 0.00 20.00 0.60 1.80 u\nP Carol 90.000 64.000 %.3f 0.00 0.00 20.00 0.60 1.80 u\n",
                  token.c_str(), static_cast<unsigned long long>(seq), static_cast<long long>(1000000000LL + static_cast<long long>(t * 1e9)), 20.0 * t, 100.0 + t, 20.0 * t, 95.0 + t, -45.0 + t);
    return b;
}

int main()
{
    const double seconds = std::getenv("ESP_STRESS_SECONDS") ? std::atof(std::getenv("ESP_STRESS_SECONDS")) : 1.5;

    StreamIngest ingest;
    ingest.SetAuth(true, kTok);
    Recorder recorder(1 << 16);                                          // a small ring: it will overflow and drop, which must be counted
    CHECK(recorder.Start("stress.esprec"));

    std::atomic<bool> stop{false};
    std::atomic<uint64_t> produced{0}, accepted{0}, wrongTokens{0}, skipped{0}, duplicated{0};

    // ---- the UDP thread ----
    std::thread producer([&]
    {
        uint64_t seq = 0, i = 0;
        const auto t0 = Clock::now();
        while (!stop.load(std::memory_order_relaxed))
        {
            const double t = std::chrono::duration<double>(Clock::now() - t0).count();
            ++i;
            if (i % 53 == 0) { ++seq; ++skipped; }                       // a datagram that "never arrived"
            ++seq;
            const std::string d = Datagram(seq, t, kTok);
            if (i % 97 == 0)                                             // a stranger with the wrong token (it does not use up a number)
            {
                const std::string w = Datagram(seq, t, std::string(32, 'f'));
                recorder.Record(w.data(), w.size(), Clock::now());
                ++produced;
                CHECK(!ingest.OnDatagram(w.data(), w.size()));
                ++wrongTokens;
            }
            recorder.Record(d.data(), d.size(), Clock::now());           // the tee sees every datagram, before the check
            ++produced;
            if (ingest.OnDatagram(d.data(), d.size())) ++accepted;
            if (i % 211 == 0)                                            // the same datagram twice
            {
                recorder.Record(d.data(), d.size(), Clock::now());
                ++produced;
                CHECK(!ingest.OnDatagram(d.data(), d.size()));
                ++duplicated;
            }
            if (i % 16 == 0) std::this_thread::yield();                  // let the other threads in; ~100k datagrams/s otherwise
        }
    });

    // ---- the render thread ----
    std::atomic<uint64_t> renderFrames{0}, renderSnapshots{0};
    bool renderOk = true;
    std::thread render([&]
    {
        Smoother smoother;
        WorldSnapshot out;
        uint64_t last = 0;
        while (!stop.load(std::memory_order_relaxed))
        {
            if (auto s = ingest.Latest(); s && s->seq != last)
            {
                renderOk = renderOk && s->seq > last && s->localValid && s->players.size() == 2 && s->protocolVersion == 5 && s->token == kTok;
                last = s->seq;
                smoother.Push(*s);
                ++renderSnapshots;
            }
            if (smoother.HasData())
            {
                smoother.PredictInto(Clock::now(), true, out);
                renderOk = renderOk && out.players.size() == 2 && out.players[0].name == "Bob";
            }
            ++renderFrames;
        }
    });

    // ---- the HUD thread ----
    bool hudOk = true;
    std::thread hud([&]
    {
        uint64_t lastPublished = 0, lastNumbered = 0, lastLost = 0;
        while (!stop.load(std::memory_order_relaxed))
        {
            const uint64_t p = ingest.PacketCount();
            const auto l = ingest.Link();
            (void)ingest.LinkSummary();
            (void)ingest.ProtocolProblem(Clock::now());
            (void)ingest.LastPacketTime();
            hudOk = hudOk && p >= lastPublished && l.numbered >= lastNumbered && ingest.AuthFailures() <= ingest.RejectedCount() && l.lost + l.reordered >= lastLost;
            lastPublished = p;
            lastNumbered = l.numbered;
            lastLost = l.lost;
            std::this_thread::yield();
        }
    });

    // ---- replay driver + a thread that fiddles with it ----
    ReplayPlayer player;
    StreamIngest replayIngest;
    replayIngest.SetSeqCheck(false);
    CHECK(player.Load(std::string(ESP_TEST_DATA_DIR) + "/synthetic_walk.esprec"));
    player.SetLoop(true);
    player.SetSpeed(20);
    ReplayDriver driver(player, replayIngest);
    driver.Start();
    std::atomic<uint64_t> replayReads{0};
    std::thread control([&]
    {
        uint64_t n = 0;
        while (!stop.load(std::memory_order_relaxed))
        {
            switch (n++ % 6)
            {
            case 0: driver.TogglePause(); break;
            case 1: driver.RequestStep(); break;
            case 2: driver.SetSpeed(0.5 + static_cast<double>(n % 7)); break;
            case 3: driver.TogglePause(); break;
            default: break;
            }
            if (auto s = replayIngest.Latest()) { if (s->players.size() == 3) ++replayReads; }
            (void)driver.Position();
            (void)driver.Speed();
            (void)driver.Paused();
            std::this_thread::sleep_for(std::chrono::microseconds(300));
        }
    });

    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    stop = true;
    producer.join();
    render.join();
    hud.join();
    control.join();
    driver.Stop();
    recorder.Stop();

    // ---- what must be true afterwards ----
    CHECK(renderOk);
    CHECK(hudOk);
    CHECK(produced > 1000 && renderSnapshots > 100 && renderFrames > 100);
    CHECK(ingest.PacketCount() == accepted.load());
    const auto link = ingest.Link();
    CHECK(link.duplicates == duplicated.load());
    CHECK(link.lost == skipped.load());                                      // exactly the numbers the producer skipped
    CHECK(link.reordered == 0 && link.sessions == 1);
    CHECK(ingest.AuthFailures() == wrongTokens.load());
    CHECK(ingest.RejectedCount() == wrongTokens.load());                      // duplicates are dropped by the sequence check, not "rejected"
    CHECK(recorder.Recorded() + recorder.Dropped() == produced.load());       // every datagram was either queued or counted as dropped
    CHECK(recorder.Recorded() > 0 && !recorder.Failed());
    CHECK(replayIngest.PacketCount() > 100 && replayReads > 0);

    // the recording is a valid, complete .esprec with exactly the queued datagrams, in order
    {
        std::ifstream f("stress.esprec", std::ios::binary);
        esprec::Reader r(f);
        esprec::Header h;
        CHECK(r.ReadHeader(h) == esprec::Status::Ok && h.wireVersion == 5);
        uint64_t n = 0, t = 0, lastT = 0;
        std::string p;
        bool ordered = true, whole = true;
        while (r.Next(t, p) == esprec::Status::Ok)
        {
            ++n;
            ordered = ordered && t >= lastT;
            lastT = t;
            whole = whole && p.rfind("MCRADAR 5\nK ", 0) == 0 && p.size() > 200;
        }
        CHECK(n == recorder.Recorded() && ordered && whole && r.NonMonotonic() == 0);
        std::printf("stress: %llu datagrams (%llu recorded, %llu dropped by the small ring), %llu accepted, %llu wrong token, %llu lost, %llu duplicates; render %llu frames / %llu snapshots; replay %llu snapshots\n",
                    static_cast<unsigned long long>(produced.load()), static_cast<unsigned long long>(recorder.Recorded()), static_cast<unsigned long long>(recorder.Dropped()),
                    static_cast<unsigned long long>(accepted.load()), static_cast<unsigned long long>(wrongTokens.load()), static_cast<unsigned long long>(link.lost),
                    static_cast<unsigned long long>(link.duplicates), static_cast<unsigned long long>(renderFrames.load()), static_cast<unsigned long long>(renderSnapshots.load()),
                    static_cast<unsigned long long>(replayIngest.PacketCount()));
    }

    std::printf(fails ? "\n%d FAILURES\n" : "\nALL OK\n", fails);
    return fails;
}
