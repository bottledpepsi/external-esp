// InjectPolicy.h - when to (re)start the agent injection for the current Minecraft process. Portable; tests/inject_policy_test.cpp drives it with fake time.
//
//   * a new target PID is tried at once (after the usual 2 s of silence)
//   * a FAILED injection is retried with growing delays (3, 6, 12, 24, then every 30 s) instead of every 30 s regardless: a transient failure (the game was
//     still starting) recovers quickly, a permanent one (a JVM that refuses the attach) stops costing a helper JVM launch every few seconds
//   * a SUCCESSFUL injection with no data (no world joined yet) is repeated every 30 s, as before - harmless, the agent just takes the options over again
//   * data from the agent resets everything; autoInject off never starts anything
#pragma once
#include <algorithm>
#include <chrono>

class InjectPolicy
{
public:
    using Clock = std::chrono::steady_clock;

    struct Query
    {
        Clock::time_point now;
        unsigned long pid = 0; // the current target
        std::chrono::milliseconds silentFor{0}; // no datagram for this long (since attach / last datagram)
        bool autoInject = true;
        bool running = false; // an injection is in progress
    };

    bool ShouldStart(const Query& q) const
    {
        if (!q.autoInject || q.running || q.silentFor <= std::chrono::seconds(2)) return false;
        if (q.pid != pid_) return true;
        return q.now >= nextAllowed_;
    }

    void OnStarted(const Query& q)
    {
        pid_ = q.pid;
        nextAllowed_ = q.now + std::chrono::seconds(30); // until we know how it went: the old fixed rate
    }

    void OnFailed(Clock::time_point now)
    {
        ++failures_;
        nextAllowed_ = now + Delay(failures_);
    }

    void OnSucceeded(Clock::time_point now)
    {
        failures_ = 0;
        nextAllowed_ = now + std::chrono::seconds(30);
    }

    void OnData() { failures_ = 0; }
    void OnTargetGone() { pid_ = 0; failures_ = 0; nextAllowed_ = {}; }

    int Failures() const { return failures_; }

    // Seconds until the next attempt is allowed (0 = now), for "retrying in N s".
    int RetryInSeconds(Clock::time_point now) const
    {
        if (nextAllowed_ <= now) return 0;
        return static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(nextAllowed_ - now).count()) + 1;
    }

    static std::chrono::seconds Delay(int failures)
    {
        const int d = 3 << std::min(std::max(failures - 1, 0), 4); // 3, 6, 12, 24, 48 ...
        return std::chrono::seconds(std::min(d, 30)); // ... capped at 30
    }

private:
    unsigned long pid_ = 0;
    int failures_ = 0;
    Clock::time_point nextAllowed_{};
};
