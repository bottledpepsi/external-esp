// shutdown_test - exit requests, the step sequence (order, failures, budgets) and the ordering rules between the real steps.
#include "Shutdown.h"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <thread>
using namespace exitflow;
using namespace std::chrono_literals;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

static int IndexOf(const char* n)
{
    for (size_t i = 0; i < std::size(kOrder); ++i) if (std::string(kOrder[i]) == n) return static_cast<int>(i);
    return -1;
}

int main()
{
    // ---- the first reason wins ----
    {
        ExitRequest r;
        CHECK(!r.Requested() && r.Why() == Reason::None);
        CHECK(r.Request(Reason::CtrlC));
        CHECK(!r.Request(Reason::WindowClosed));       // a second request (the window closing as a consequence) does not change why
        CHECK(r.Requested() && r.Why() == Reason::CtrlC);
        CHECK(std::string(Name(Reason::CtrlC)) == "Ctrl+C" && std::string(Name(Reason::None)) == "(no reason)");
    }
    // ---- from another thread, and the console-close handler's wait ----
    {
        ExitRequest r;
        std::thread t([&] { r.Request(Reason::ConsoleClosed); });
        t.join();
        CHECK(r.Why() == Reason::ConsoleClosed);
        CHECK(!r.WaitDone(50ms));                       // the main thread has not finished: the handler keeps waiting (up to its limit)
        std::thread main([&] { std::this_thread::sleep_for(100ms); r.MarkDone(); });
        CHECK(r.WaitDone(3s));                          // released as soon as the shutdown is done, not after the whole timeout
        main.join();
        CHECK(r.WaitDone(1ms));                         // and stays released
    }
    // ---- order: kOrder, whatever the registration order; unknown names run last ----
    {
        std::vector<std::string> ran;
        Sequence s;
        for (const char* n : {"timer", "producers", "extra", "hotkeys", "recorder", "agent", "settings", "skins"})
            s.Add(n, [&ran, n] { ran.push_back(n); });
        const auto res = s.Run();
        CHECK((ran == std::vector<std::string>{"hotkeys", "settings", "agent", "producers", "recorder", "skins", "timer", "extra"}));
        CHECK(res.size() == 8 && std::all_of(res.begin(), res.end(), [](const StepResult& r) { return r.ok && !r.overBudget; }));
        Sequence only;                                  // a sequence with some steps missing (replay mode has no agent) just skips them
        std::vector<std::string> r2;
        only.Add("recorder", [&] { r2.push_back("recorder"); });
        only.Add("hotkeys", [&] { r2.push_back("hotkeys"); });
        only.Run();
        CHECK((r2 == std::vector<std::string>{"hotkeys", "recorder"}));
    }
    // ---- a failing or slow step does not stop the others, and is reported ----
    {
        std::vector<std::string> ran;
        Sequence s;
        s.Add("hotkeys", [&] { ran.push_back("hotkeys"); throw std::runtime_error("boom"); });
        s.Add("agent", [&] { ran.push_back("agent"); std::this_thread::sleep_for(120ms); }, 50ms);
        s.Add("producers", [&] { ran.push_back("producers"); throw 42; });
        s.Add("recorder", [&] { ran.push_back("recorder"); });
        const auto r = s.Run();
        CHECK(ran.size() == 4 && ran.back() == "recorder");
        CHECK(!r[0].ok && r[0].error == "boom");
        CHECK(r[1].ok && r[1].overBudget && r[1].elapsed >= 100ms);
        CHECK(!r[2].ok && r[2].error == "unknown error");
        CHECK(r[3].ok);
        const std::string sum = Sequence::Summary(r);
        CHECK(sum.find("hotkeys") == 0 && sum.find("FAILED: boom") != std::string::npos && sum.find("agent") != std::string::npos && sum.find("(slow)") != std::string::npos);
    }
    // ---- the rules between the real steps ----
    {
        CHECK(IndexOf("hotkeys") == 0);                                   // keys go back to the system first: nothing below may keep a user waiting for them
        CHECK(IndexOf("settings") < IndexOf("agent"));                    // what the user changed is on the disk before the (slower) agent step
        CHECK(IndexOf("agent") < IndexOf("producers"));                   // the attach helper reports through the overlay's state; stop talking to the game before the listeners go
        CHECK(IndexOf("producers") < IndexOf("recorder"));                // the recorder drains and closes the file only after nothing writes to it any more
        CHECK(IndexOf("timer") == static_cast<int>(std::size(kOrder)) - 1 || IndexOf("skins") < IndexOf("timer"));
        for (const char* n : kOrder) CHECK(IndexOf(n) >= 0);
        std::vector<std::string> u(std::begin(kOrder), std::end(kOrder));
        std::sort(u.begin(), u.end());
        CHECK(std::adjacent_find(u.begin(), u.end()) == u.end());         // no name twice
    }

    if (fails) std::printf("%d failure(s)\n", fails); else std::puts("shutdown_test: all passed");
    return fails ? 1 : 0;
}
