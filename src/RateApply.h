// RateApply.h - decides WHEN a new agent rate (the AgentHz slider) is sent to the running game. Portable.
//
// The agent already supports being loaded a second time with different options (agent/src/mcradar/Agent.java reload(); AgentReloadTest checks that a
// hz change takes effect in the running JVM), so "send the new rate" = run the attach helper again with the new hz. That costs a helper JVM (about a
// second), so the policy waits until the slider has stopped, never overlaps two attaches, and gives up after a few failures instead of spawning
// a JVM every few seconds for ever.
#pragma once
#include <chrono>
#include <cstdio>

class RateApplier
{
public:
    using Clock = std::chrono::steady_clock;
    static constexpr int kMaxFailures = 3;
    static constexpr std::chrono::milliseconds kQuiet{600}; // the wish must stay unchanged this long (a slider drag is one change)
    static constexpr std::chrono::seconds kRetryAfter{5};

    enum class Note { None, Waiting, Applying, Applied, Failed, Unavailable };

    struct Query
    {
        Clock::time_point now;
        bool canApply = false; // a game is attached, auto-inject is on, no demo/replay
        bool busy = false; // an attach is running right now (the first injection, or ours)
    };

    explicit RateApplier(int running = 120) : applied_(running), want_(running) {}

    // The user (or a profile) asked for `hz`.
    void Want(int hz, Clock::time_point now)
    {
        if (hz == want_) return;
        want_ = hz;
        wantAt_ = now;
        failures_ = 0;
        retryAt_ = {};
    }

    // Nothing needs sending: no agent is running yet (the next injection uses the configured rate), or a fresh injection just used it.
    void SetRunning(int hz)
    {
        applied_ = want_ = hz;
        failures_ = 0;
        retryAt_ = {};
    }

    // The hz to send now, or 0.
    int Poll(const Query& q) const
    {
        if (want_ == applied_ || inflight_ != 0 || failures_ >= kMaxFailures) return 0;
        if (!q.canApply || q.busy) return 0;
        if (q.now - wantAt_ < kQuiet) return 0;
        if (retryAt_ != Clock::time_point{} && q.now < retryAt_) return 0;
        return want_;
    }

    void Started(int hz) { inflight_ = hz; }

    void Finished(bool ok, Clock::time_point now)
    {
        if (inflight_ == 0) return;
        if (ok)
        {
            applied_ = inflight_;
            failures_ = 0;
            retryAt_ = {};
        }
        else
        {
            ++failures_;
            retryAt_ = now + kRetryAfter;
        }
        inflight_ = 0;
    }

    int Applied() const { return applied_; }
    int Wanted() const { return want_; }
    bool InFlight() const { return inflight_ != 0; }
    bool GaveUp() const { return want_ != applied_ && failures_ >= kMaxFailures; }

    // What to tell the user next to the slider.
    Note CurrentNote(bool canApply) const
    {
        if (inflight_ != 0) return Note::Applying;
        if (want_ == applied_) return everApplied_ ? Note::Applied : Note::None;
        if (GaveUp()) return Note::Failed;
        return canApply ? Note::Waiting : Note::Unavailable;
    }
    void MarkShown() { everApplied_ = true; } // after a successful change, so "Applied" is shown (not for the initial state)

    // The sentence for the ADVANCED tab (<= 72 characters so it fits one line); `warn` is set for the two problem notes.
    static const char* Describe(Note n, int hz, char* buf, size_t cap, bool* warn)
    {
        if (warn) *warn = n == Note::Failed || n == Note::Unavailable;
        switch (n)
        {
        case Note::Waiting: std::snprintf(buf, cap, "Will be sent to the game when you stop moving the slider."); break;
        case Note::Applying: std::snprintf(buf, cap, "Sending %d Hz to the game ...", hz); break;
        case Note::Applied: std::snprintf(buf, cap, "The game agent now runs at %d Hz.", hz); break;
        case Note::Failed: std::snprintf(buf, cap, "Could not change the running game (esp_log.txt). Restart it."); break;
        case Note::Unavailable: std::snprintf(buf, cap, "Auto-inject is off: start the game with hz=%d or use --hz.", hz); break;
        default: if (cap) buf[0] = 0; break;
        }
        return buf;
    }
private:
    int applied_, want_, inflight_ = 0, failures_ = 0;
    Clock::time_point wantAt_{}, retryAt_{};
    bool everApplied_ = false;
};
