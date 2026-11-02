// Shutdown.h - leaving cleanly (namespace exitflow: "shutdown" would collide with winsock's shutdown()). Portable.
//   ExitRequest : why the program was asked to end (hotkey, window closed, Ctrl+C, console closed, log-off), from any thread; the first reason wins.
//                 The console-close handler waits on it (WaitDone) so Windows does not kill the process half-way through the shutdown.
//   Sequence    : the shutdown steps, in one fixed order with a time budget each. A step that throws or runs long is reported and the rest still run.
//   kOrder      : that order, as names. tests/shutdown_test.cpp checks the dependencies (producers stop before the recorder drains; the agent is
//                 paused before the process that talks to it goes away; hotkeys are given back first).
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace exitflow
{
    enum class Reason { None, Hotkey, WindowClosed, CtrlC, ConsoleClosed, LogOff, Error };
    const char* Name(Reason r);

    class ExitRequest
    {
    public:
        // Returns true when this call set the reason (the first one wins; later ones are ignored).
        bool Request(Reason r)
        {
            int expected = 0;
            return reason_.compare_exchange_strong(expected, static_cast<int>(r));
        }
        bool Requested() const { return reason_.load() != 0; }
        Reason Why() const { return static_cast<Reason>(reason_.load()); }

        // The main thread says it has finished shutting down; a waiter (the console-close handler) is released.
        void MarkDone()
        {
            {
                std::lock_guard<std::mutex> l(m_);
                done_ = true;
            }
            cv_.notify_all();
        }
        // True when MarkDone() came within `timeout`.
        bool WaitDone(std::chrono::milliseconds timeout)
        {
            std::unique_lock<std::mutex> l(m_);
            return cv_.wait_for(l, timeout, [&] { return done_; });
        }
        void ResetForTests()
        {
            reason_ = 0;
            std::lock_guard<std::mutex> l(m_);
            done_ = false;
        }

    private:
        std::atomic<int> reason_{0};
        std::mutex m_;
        std::condition_variable cv_;
        bool done_ = false;
    };

    ExitRequest& Global(); // the one the overlay loop, the hotkey and the console handler share

    // The order of the steps of the overlay's shutdown.
    inline constexpr const char* kOrder[] = {"hotkeys", "settings", "agent", "producers", "recorder", "skins", "timer"};

    struct StepResult
    {
        std::string name;
        bool ok = true;
        bool overBudget = false;
        std::chrono::milliseconds elapsed{0};
        std::string error;
    };

    class Sequence
    {
    public:
        using Clock = std::chrono::steady_clock;
        // Registers the step called `name`; the run order is kOrder, not the order of Add().
        void Add(const std::string& name, std::function<void()> fn, std::chrono::milliseconds budget = std::chrono::milliseconds(1000));
        // Runs every registered step in kOrder (unregistered names are skipped; a registered name that is not in kOrder is reported and run last).
        std::vector<StepResult> Run();
        static std::string Summary(const std::vector<StepResult>& r); // "hotkeys 0 ms, agent 912 ms, producers 3 ms ..." for the log

    private:
        struct Step
        {
            std::string name;
            std::function<void()> fn;
            std::chrono::milliseconds budget;
        };
        std::vector<Step> steps_;
    };
} // namespace exitflow
