// inject_policy_test.cpp - the retry policy of the agent injection, with fake time (src/InjectPolicy.h).
#include "../src/InjectPolicy.h"
#include <cstdio>

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

using namespace std::chrono_literals;
using Clock = InjectPolicy::Clock;

static InjectPolicy::Query Q(Clock::time_point now, unsigned long pid, std::chrono::milliseconds silent = 10s, bool running = false, bool autoInject = true)
{
    InjectPolicy::Query q;
    q.now = now; q.pid = pid; q.silentFor = silent; q.running = running; q.autoInject = autoInject;
    return q;
}

int main()
{
    const Clock::time_point t0 = Clock::time_point{} + 1000s;
    {   // delays: 3, 6, 12, 24, 30, 30 ...
        CHECK(InjectPolicy::Delay(1) == 3s && InjectPolicy::Delay(2) == 6s && InjectPolicy::Delay(3) == 12s && InjectPolicy::Delay(4) == 24s);
        CHECK(InjectPolicy::Delay(5) == 30s && InjectPolicy::Delay(6) == 30s && InjectPolicy::Delay(50) == 30s && InjectPolicy::Delay(0) == 3s);
    }
    {   // a new PID is tried at once, but only after 2 s of silence
        InjectPolicy p;
        CHECK(!p.ShouldStart(Q(t0, 100, 1s)));
        CHECK(p.ShouldStart(Q(t0, 100, 3s)));
        CHECK(!p.ShouldStart(Q(t0, 100, 10s, true)));        // already running
        CHECK(!p.ShouldStart(Q(t0, 100, 10s, false, false))); // auto-inject off
    }
    {   // failures back off; the same PID is not retried before the delay
        InjectPolicy p;
        auto t = t0;
        p.OnStarted(Q(t, 100));
        t += 2s; p.OnFailed(t);                                // 1st failure -> 3 s
        CHECK(!p.ShouldStart(Q(t + 2s, 100)) && p.ShouldStart(Q(t + 3s, 100)));
        CHECK(p.RetryInSeconds(t + 1s) == 3);
        t += 3s; p.OnStarted(Q(t, 100)); t += 1s; p.OnFailed(t);   // 2nd -> 6 s
        CHECK(!p.ShouldStart(Q(t + 5s, 100)) && p.ShouldStart(Q(t + 6s, 100)));
        for (int i = 0; i < 6; ++i) { t += 40s; p.OnStarted(Q(t, 100)); p.OnFailed(t); }
        CHECK(p.Failures() == 8 && !p.ShouldStart(Q(t + 29s, 100)) && p.ShouldStart(Q(t + 30s, 100)));   // capped at 30 s
    }
    {   // a different PID restarts the story at once, whatever the failures were
        InjectPolicy p;
        p.OnStarted(Q(t0, 100)); p.OnFailed(t0); p.OnFailed(t0); p.OnFailed(t0);
        CHECK(!p.ShouldStart(Q(t0 + 1s, 100)));
        CHECK(p.ShouldStart(Q(t0 + 1s, 101)));
        p.OnStarted(Q(t0 + 1s, 101));
        CHECK(!p.ShouldStart(Q(t0 + 5s, 101)));
    }
    {   // success without data (no world yet): repeated every 30 s like before; data resets the failure count
        InjectPolicy p;
        p.OnStarted(Q(t0, 100)); p.OnFailed(t0); p.OnFailed(t0);
        p.OnSucceeded(t0 + 10s);
        CHECK(p.Failures() == 0 && !p.ShouldStart(Q(t0 + 39s, 100)) && p.ShouldStart(Q(t0 + 40s, 100)));
        p.OnFailed(t0 + 50s); CHECK(p.Failures() == 1);
        p.OnData(); CHECK(p.Failures() == 0);
    }
    {   // the game closes and starts again with the same PID value (reuse) or another: the target is forgotten, the next one is tried at once
        InjectPolicy p;
        p.OnStarted(Q(t0, 100)); p.OnFailed(t0);
        p.OnTargetGone();
        CHECK(p.ShouldStart(Q(t0 + 1s, 100)) && p.Failures() == 0 && p.RetryInSeconds(t0) == 0);
    }
    std::printf(g_fail == 0 ? "ALL OK (%d checks)\n" : "%d FAILURES\n", g_fail == 0 ? g_checks : g_fail);
    return g_fail == 0 ? 0 : 1;
}
