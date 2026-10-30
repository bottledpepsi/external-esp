// GpuRecovery.h - what to do after the GPU device is lost (driver reset, TDR, adapter removed, GPU switch, remote session change). Portable: the decision
// logic only, no Windows types; tests/gpu_recovery_test.cpp injects failures with fake time. Gfx performs the actions it returns.
//
//   Healthy --OnLoss--> Lost --Poll(after the backoff)--> TryRecover --OnRecovered--> Healthy (on probation)
//                         |                                   '--OnRecoverFailed--> Lost (longer backoff)
//                         '--Poll--> GiveUp   after 5 failed recoveries in a row, or when the device was lost 4 times within 30 s (it recovers and
//                                             dies again: a broken driver), or when losses keep coming faster than they can be recovered
// GiveUp means: stop trying the GPU renderer and switch to the legacy GDI one (it needs no D3D device). Recovery attempts are spaced 250 ms, 500 ms,
// 1 s, 2 s, 4 s so that a driver that is still resetting is not hammered; a device that stayed healthy for 30 s forgets its history.
#pragma once
#include <chrono>
#include <deque>

class GpuRecovery
{
public:
    using Clock = std::chrono::steady_clock;
    enum class Action { None, TryRecover, GiveUp };

    static constexpr int kMaxConsecutiveFailures = 5;
    static constexpr int kMaxLossesInWindow = 4;
    static constexpr std::chrono::seconds kFlapWindow{30};
    static constexpr std::chrono::seconds kHealthyFor{30};

    // The device was found lost (Present, EndDraw, ResizeBuffers ...). Repeated reports of the same incident while already Lost are ignored.
    void OnLoss(Clock::time_point now)
    {
        if (lost_) return;
        lost_ = true;
        ++totalLosses_;
        recent_.push_back(now);
        while (!recent_.empty() && now - recent_.front() > kFlapWindow) recent_.pop_front();
        nextTry_ = now + Backoff(consecutiveFailures_);
    }

    Action Poll(Clock::time_point now)
    {
        if (gaveUp_) return Action::GiveUp;
        if (!lost_)
        {
            if (healthySince_ != Clock::time_point{} && now - healthySince_ >= kHealthyFor)
            {
                recent_.clear();                // a long quiet spell: the earlier incidents no longer count
                consecutiveFailures_ = 0;
                healthySince_ = {};
            }
            return Action::None;
        }
        if (consecutiveFailures_ >= kMaxConsecutiveFailures || static_cast<int>(recent_.size()) >= kMaxLossesInWindow)
        {
            gaveUp_ = true;
            return Action::GiveUp;
        }
        return now >= nextTry_ ? Action::TryRecover : Action::None;
    }

    void OnRecovered(Clock::time_point now)
    {
        lost_ = false;
        ++recoveries_;
        healthySince_ = now;                     // on probation: a loss within kFlapWindow counts towards giving up
    }

    void OnRecoverFailed(Clock::time_point now)
    {
        ++consecutiveFailures_;
        nextTry_ = now + Backoff(consecutiveFailures_);
    }

    bool Lost() const { return lost_; }
    bool GaveUp() const { return gaveUp_; }
    int TotalLosses() const { return totalLosses_; }
    int Recoveries() const { return recoveries_; }
    int ConsecutiveFailures() const { return consecutiveFailures_; }

    static std::chrono::milliseconds Backoff(int failures)
    {
        static const int ms[] = {250, 500, 1000, 2000, 4000};
        return std::chrono::milliseconds(ms[failures < 0 ? 0 : failures > 4 ? 4 : failures]);
    }

private:
    bool lost_ = false, gaveUp_ = false;
    int consecutiveFailures_ = 0, totalLosses_ = 0, recoveries_ = 0;
    Clock::time_point nextTry_{}, healthySince_{};
    std::deque<Clock::time_point> recent_;
};
