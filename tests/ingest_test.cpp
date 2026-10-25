// StreamIngest (parse + stamp + publish, no socket) driven by a fake clock.
#include "StreamIngest.h"
#include <cstdio>
#include <cstring>
#include <thread>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

using Clock = StreamIngest::Clock;
using std::chrono::milliseconds;

static const char kV4[] = "MCRADAR 4\nT 1000\nS ok\nL Alice 1 2 3 10 20 17.5\nP Bob 4 5 6 - - -\n";

int main()
{
    // fake clock: the test owns the time
    Clock::time_point fake = Clock::time_point(std::chrono::seconds(100));
    StreamIngest in([&] { return fake; });

    // nothing yet
    CHECK(in.Latest() == nullptr && in.LastPacketTime() == Clock::time_point{} && in.PacketCount() == 0 && in.RejectedCount() == 0);

    // a valid datagram is stamped with the fake clock and a sequence number
    CHECK(in.OnDatagram(kV4, std::strlen(kV4)));
    auto a = in.Latest();
    CHECK(a && a->seq == 1 && a->arrived == fake && a->localValid && a->local.name == "Alice" && a->players.size() == 1);
    CHECK(a->hasSampleTime && a->sampleNs == 1000);
    CHECK(in.LastPacketTime() == fake && in.PacketCount() == 1);

    // time moves, second datagram: seq 2, new arrival time; the snapshot handed out earlier is untouched (immutable)
    fake += milliseconds(8);
    CHECK(in.OnDatagram(kV4, std::strlen(kV4)));
    auto b = in.Latest();
    CHECK(b != a && b->seq == 2 && b->arrived == fake && a->seq == 1 && a->arrived == fake - milliseconds(8));

    // garbage is rejected, counted, and changes nothing
    CHECK(!in.OnDatagram("hello", 5));
    CHECK(!in.OnDatagram("", 0));
    CHECK(in.RejectedCount() == 2 && in.PacketCount() == 2 && in.Latest() == b && in.LastPacketTime() == fake);

    // explicit arrival time (replay): used as given, the clock is not consulted
    const auto t = Clock::time_point(std::chrono::seconds(7));
    CHECK(in.OnDatagram(kV4, std::strlen(kV4), t));
    CHECK(in.Latest()->seq == 3 && in.Latest()->arrived == t && in.LastPacketTime() == t);

    // Publish() of a pre-parsed snapshot (what the socket thread does after draining) numbers it the same way
    auto s = std::make_shared<WorldSnapshot>();
    s->status = "x";
    in.Publish(s, fake);
    CHECK(in.Latest()->seq == 4 && in.Latest()->status == "x" && in.PacketCount() == 4);

    // an arrival time of exactly 0 is a real time, not "nothing yet"
    StreamIngest z;
    z.OnDatagram(kV4, std::strlen(kV4), Clock::time_point{});
    CHECK(z.PacketCount() == 1 && z.Latest() != nullptr);

    // the default clock is the steady clock
    StreamIngest d;
    const auto before = Clock::now();
    d.OnDatagram(kV4, std::strlen(kV4));
    CHECK(d.Latest()->arrived >= before && d.Latest()->arrived <= Clock::now());

    // one producer, one reader, concurrently: the reader only ever sees complete, increasing snapshots
    {
        StreamIngest c;
        std::atomic<bool> stop{false};
        std::thread producer([&] { for (int i = 0; i < 20000; ++i) c.OnDatagram(kV4, std::strlen(kV4), Clock::time_point(std::chrono::microseconds(i + 1))); stop = true; });
        uint64_t last = 0;
        bool ok = true;
        while (!stop)
        {
            if (auto p = c.Latest())
            {
                if (p->seq < last || !p->localValid || p->players.size() != 1) ok = false;
                last = p->seq;
            }
        }
        producer.join();
        CHECK(ok && c.PacketCount() == 20000 && c.Latest()->seq == 20000);
    }

    std::printf(fails ? "\n%d FAILURES\n" : "\nALL OK\n", fails);
    return fails;
}
