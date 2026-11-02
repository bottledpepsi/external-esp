// rate_apply_test - when a changed agent rate is sent to the running game.
#include "RateApply.h"
#include <cstdio>
#include <cstring>
using namespace std::chrono;
using namespace std::chrono_literals;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

static const RateApplier::Clock::time_point T0 = RateApplier::Clock::now();
static RateApplier::Query Q(milliseconds at, bool can = true, bool busy = false) { return {T0 + at, can, busy}; }
static RateApplier::Clock::time_point At(milliseconds ms) { return T0 + ms; }

int main()
{
    // ---- nothing wanted, nothing sent ----
    {
        RateApplier r(120);
        CHECK(r.Poll(Q(0ms)) == 0 && r.Poll(Q(10s)) == 0);
        r.Want(120, At(0ms));                          // the same value again is not a change
        CHECK(r.Poll(Q(10s)) == 0);
    }
    // ---- a slider drag is one apply, after it rests ----
    {
        RateApplier r(120);
        for (int i = 0; i < 20; ++i) r.Want(100 - i, At(milliseconds(i * 30)));   // moved for 600 ms
        CHECK(r.Wanted() == 81);
        CHECK(r.Poll(Q(600ms)) == 0);                   // still within the quiet time of the last change (at 570 ms)
        CHECK(r.Poll(Q(1000ms)) == 0);                  // 430 ms since the last change
        CHECK(r.Poll(Q(1170ms)) == 81);                 // 600 ms since the last change: send the final value, once
        r.Started(81);
        CHECK(r.Poll(Q(1200ms)) == 0 && r.InFlight());  // never two at a time
        r.Finished(true, At(2000ms));
        CHECK(r.Applied() == 81 && r.Poll(Q(30s)) == 0);
    }
    // ---- conditions: no game / the injector busy ----
    {
        RateApplier r(120);
        r.Want(60, At(0ms));
        CHECK(r.Poll(Q(5s, false)) == 0);              // not possible now (no game, auto-inject off): it stays wanted
        CHECK(r.Poll(Q(5s, true, true)) == 0);         // an attach is running
        CHECK(r.Poll(Q(5s)) == 60);
    }
    // ---- failure: retry after the back-off, give up after kMaxFailures, a new wish starts over ----
    {
        RateApplier r(120);
        r.Want(60, At(0ms));
        int sent = 0;
        for (milliseconds t = 1s; t < 120s; t += 250ms)
        {
            if (const int hz = r.Poll(Q(t)); hz) { ++sent; r.Started(hz); r.Finished(false, At(t)); }
        }
        CHECK(sent == RateApplier::kMaxFailures);       // 3 tries in two minutes, then it stops spawning JVMs
        CHECK(r.GaveUp() && r.CurrentNote(true) == RateApplier::Note::Failed);
        // the back-off: the retry is not immediate
        RateApplier b(120);
        b.Want(60, At(0ms));
        CHECK(b.Poll(Q(1s)) == 60);
        b.Started(60);
        b.Finished(false, At(1s));
        CHECK(b.Poll(Q(2s)) == 0 && b.Poll(Q(5900ms)) == 0 && b.Poll(Q(6s)) == 60);
        // a different value revives it
        r.Want(90, At(200s));
        CHECK(!r.GaveUp() && r.Poll(Q(201s)) == 90);
    }
    // ---- the wish changes while an attach is in flight ----
    {
        RateApplier r(120);
        r.Want(60, At(0ms));
        CHECK(r.Poll(Q(1s)) == 60);
        r.Started(60);
        r.Want(200, At(1500ms));
        r.Finished(true, At(2000ms));
        CHECK(r.Applied() == 60 && r.Wanted() == 200);
        CHECK(r.Poll(Q(2099ms)) == 0);                  // quiet time counts from the last change (1.5 s), not from the finish
        CHECK(r.Poll(Q(2100ms)) == 200);
    }
    // ---- going back to the applied value cancels the pending change ----
    {
        RateApplier r(120);
        r.Want(60, At(0ms));
        r.Want(120, At(100ms));
        CHECK(r.Poll(Q(10s)) == 0 && r.CurrentNote(true) == RateApplier::Note::None);
    }
    // ---- SetRunning: nothing to send when no agent runs / after a fresh injection ----
    {
        RateApplier r(120);
        r.Want(60, At(0ms));
        r.SetRunning(60);
        CHECK(r.Poll(Q(10s)) == 0 && r.Applied() == 60 && r.Wanted() == 60);
    }
    // ---- notes and their text (one line: <= 72 characters) ----
    {
        RateApplier r(120);
        using N = RateApplier::Note;
        CHECK(r.CurrentNote(true) == N::None);
        r.Want(60, At(0ms));
        CHECK(r.CurrentNote(true) == N::Waiting && r.CurrentNote(false) == N::Unavailable);
        r.Started(60);
        CHECK(r.CurrentNote(true) == N::Applying);
        r.Finished(true, At(1s));
        r.MarkShown();
        CHECK(r.CurrentNote(true) == N::Applied);
        char b[128];
        bool warn = true;
        for (N n : {N::Waiting, N::Applying, N::Applied, N::Failed, N::Unavailable})
        {
            RateApplier::Describe(n, 240, b, sizeof b, &warn);
            CHECK(std::strlen(b) > 10 && std::strlen(b) <= 72);
            CHECK(warn == (n == N::Failed || n == N::Unavailable));
        }
        RateApplier::Describe(N::Applied, 90, b, sizeof b, &warn);
        CHECK(std::strcmp(b, "The game agent now runs at 90 Hz.") == 0);
        RateApplier::Describe(N::None, 90, b, sizeof b, &warn);
        CHECK(b[0] == 0 && !warn);
    }

    if (fails) std::printf("%d failure(s)\n", fails); else std::puts("rate_apply_test: all passed");
    return fails ? 1 : 0;
}
