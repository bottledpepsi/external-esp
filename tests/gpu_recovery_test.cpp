// gpu_recovery_test.cpp - the device-loss recovery decisions with injected failures and fake time (src/GpuRecovery.h).
#include "../src/GpuRecovery.h"
#include <cstdio>

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

using namespace std::chrono_literals;
using Clock = GpuRecovery::Clock;
using A = GpuRecovery::Action;

int main()
{
    const Clock::time_point t0 = Clock::time_point{} + 1000s;
    {   // a healthy device: nothing to do
        GpuRecovery r;
        CHECK(r.Poll(t0) == A::None && !r.Lost() && r.TotalLosses() == 0);
    }
    {   // one loss, recovered at the first try
        GpuRecovery r;
        r.OnLoss(t0);
        CHECK(r.Lost() && r.Poll(t0 + 100ms) == A::None);          // not before the 250 ms backoff
        CHECK(r.Poll(t0 + 250ms) == A::TryRecover);
        r.OnRecovered(t0 + 300ms);
        CHECK(!r.Lost() && r.Recoveries() == 1 && r.Poll(t0 + 400ms) == A::None && !r.GaveUp());
    }
    {   // the same incident reported again (Present and EndDraw both fail) counts once
        GpuRecovery r;
        r.OnLoss(t0); r.OnLoss(t0 + 5ms); r.OnLoss(t0 + 9ms);
        CHECK(r.TotalLosses() == 1);
    }
    {   // recovery keeps failing: 250, 500, 1000, 2000, 4000 ms backoffs, then give up (-> GDI)
        GpuRecovery r;
        auto t = t0;
        r.OnLoss(t);
        const int waits[] = {250, 500, 1000, 2000, 4000};
        for (int i = 0; i < 5; ++i)
        {
            CHECK(r.Poll(t + std::chrono::milliseconds(waits[i] - 1)) == A::None);
            t += std::chrono::milliseconds(waits[i]);
            CHECK(r.Poll(t) == A::TryRecover);
            r.OnRecoverFailed(t);
        }
        CHECK(r.ConsecutiveFailures() == 5 && r.Poll(t + 60s) == A::GiveUp && r.GaveUp());
        CHECK(r.Poll(t + 120s) == A::GiveUp);                      // permanent
    }
    {   // fail twice, then succeed: the counter is not what decides later; a long healthy spell clears it
        GpuRecovery r;
        auto t = t0;
        r.OnLoss(t);
        t += 250ms; CHECK(r.Poll(t) == A::TryRecover); r.OnRecoverFailed(t);
        t += 500ms; CHECK(r.Poll(t) == A::TryRecover); r.OnRecoverFailed(t);
        t += 1000ms; CHECK(r.Poll(t) == A::TryRecover); r.OnRecovered(t);
        CHECK(r.ConsecutiveFailures() == 2);
        CHECK(r.Poll(t + 31s) == A::None && r.ConsecutiveFailures() == 0);
    }
    {   // flapping: the device recovers each time and dies again within the window -> after the 4th loss in 30 s, give up
        GpuRecovery r;
        auto t = t0;
        for (int i = 0; i < 3; ++i)
        {
            r.OnLoss(t);
            t += 300ms; CHECK(r.Poll(t) == A::TryRecover); r.OnRecovered(t);
            t += 2s;
        }
        r.OnLoss(t);                                               // the 4th within 30 s
        CHECK(r.Poll(t + 10s) == A::GiveUp);
    }
    {   // slow losses (a TDR every few minutes) are each recovered, never a reason to give up
        GpuRecovery r;
        auto t = t0;
        for (int i = 0; i < 20; ++i)
        {
            r.OnLoss(t);
            t += 300ms; CHECK(r.Poll(t) == A::TryRecover); r.OnRecovered(t);
            t += 120s;
            CHECK(r.Poll(t) == A::None);
        }
        CHECK(!r.GaveUp() && r.TotalLosses() == 20 && r.Recoveries() == 20);
    }
    {   // a loss during recovery attempts after a quiet spell starts the story afresh
        GpuRecovery r;
        auto t = t0;
        r.OnLoss(t); t += 300ms; r.Poll(t); r.OnRecoverFailed(t); t += 600ms; r.Poll(t); r.OnRecovered(t);
        t += 60s; r.Poll(t);
        r.OnLoss(t);
        CHECK(r.Poll(t + 250ms) == A::TryRecover && r.ConsecutiveFailures() == 0);
    }
    std::printf(g_fail == 0 ? "ALL OK (%d checks)\n" : "%d FAILURES\n", g_fail == 0 ? g_checks : g_fail);
    return g_fail == 0 ? 0 : 1;
}
