#include "PacingTestApp.h"
#include "IntervalStats.h"
#include "SysInfo.h"
#include "Version.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <string>
#include <timeapi.h>
#include <vector>
#include <windows.h>

namespace
{
    using Clock = std::chrono::steady_clock;

    // ntdll!NtQueryTimerResolution (undocumented but stable for decades), values in 100 ns units. Which output is which has been documented both ways, so the
    // two are normalised by size: the larger period is the coarsest (the default tick, typically 15.625 ms), the smaller the finest the system can do.
    struct TimerRes
    {
        bool ok = false;
        double coarsestMs = 0, finestMs = 0, currentMs = 0;
    };

    TimerRes QueryRes()
    {
        using Fn = LONG(NTAPI*)(PULONG, PULONG, PULONG);
        TimerRes r;
        HMODULE nt = GetModuleHandleW(L"ntdll.dll");
        auto f = nt ? reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(nt, "NtQueryTimerResolution"))) : nullptr;
        ULONG a = 0, b = 0, cur = 0;
        if (f && f(&a, &b, &cur) == 0)
        {
            r.ok = true;
            r.coarsestMs = std::max(a, b) / 10000.0;
            r.finestMs = std::min(a, b) / 10000.0;
            r.currentMs = cur / 10000.0;
        }
        return r;
    }

    std::string ResText(const TimerRes& r)
    {
        if (!r.ok) return "unavailable";
        char b[160];
        std::snprintf(b, sizeof b, "current period %.3f ms (finest possible %.3f ms, coarsest %.3f ms)", r.currentMs, r.finestMs, r.coarsestMs);
        return b;
    }

    double MsSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

    // Repeats `wait` (which should take about `aimMs`) `n` times and returns how long each really took.
    std::vector<double> Measure(int n, const std::function<void()>& wait)
    {
        std::vector<double> v;
        v.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
        {
            const auto t0 = Clock::now();
            wait();
            v.push_back(MsSince(t0));
        }
        return v;
    }

    // The overlay's own frame pacing (EspOverlay::Pace): Sleep(1) while more than 2 ms remain, then yield-spin; measured as the interval between frames.
    std::vector<double> PaceLoop(int hz, double seconds)
    {
        std::vector<double> v;
        const auto period = std::chrono::microseconds(1'000'000 / hz);
        auto frame = Clock::now();
        const auto end = frame + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds));
        auto last = frame;
        while (frame < end)
        {
            const auto target = frame + period;
            for (;;)
            {
                const auto now = Clock::now();
                if (now >= target) break;
                if (target - now > std::chrono::milliseconds(2)) Sleep(1);
                else SwitchToThread();
            }
            const auto now = Clock::now();
            v.push_back(std::chrono::duration<double, std::milli>(now - last).count());
            last = now;
            frame = now;
        }
        if (!v.empty()) v.erase(v.begin()); // the first interval includes the start-up
        return v;
    }
} // namespace

int RunPacingTest(const AppOptions&, int secondsPerCase)
{
    std::string out;
    auto line = [&](const std::string& s)
    {
        out += s + "\n";
        std::puts(s.c_str());
    };
    line(std::string("mcesp ") + kEspVersion + " --pacing-test " + std::to_string(secondsPerCase));
    line("os: " + sysinfo::Collect().os);
    line("NOTE: this measures the overlay process; the agent inside the game is measured by --pacing-report (docs/pacing.md). Late = interval over 1.5x the period.");
    line("");

    const TimerRes before = QueryRes();
    line("timer resolution at start (this process has not asked for anything):  " + ResText(before));
    line("");
    const int n = 300;

    line("--- Sleep(1): how long does it really take? (aim 1 ms; " + std::to_string(n) + " samples each)");
    line(intervalstats::Row("Sleep(1), default resolution", intervalstats::Summarize(Measure(n, [] { Sleep(1); }), 1.0)));

    timeBeginPeriod(1);
    const TimerRes after = QueryRes();
    line("timer resolution after timeBeginPeriod(1):                            " + ResText(after));
    line(intervalstats::Row("Sleep(1), after timeBeginPeriod(1)", intervalstats::Summarize(Measure(n, [] { Sleep(1); }), 1.0)));

    // High-resolution waitable timer (Windows 10 1803+): a 1 ms wait that does not depend on the global tick at all.
    HANDLE hr = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /*CREATE_WAITABLE_TIMER_HIGH_RESOLUTION*/, TIMER_ALL_ACCESS);
    if (hr)
    {
        line(intervalstats::Row("high-resolution waitable timer, 1 ms", intervalstats::Summarize(Measure(n, [hr]
        {
            LARGE_INTEGER due;
            due.QuadPart = -10000; // 1 ms, relative, in 100 ns units
            SetWaitableTimer(hr, &due, 0, nullptr, nullptr, FALSE);
            WaitForSingleObject(hr, INFINITE);
        }), 1.0)));
        CloseHandle(hr);
    }
    else line("high-resolution waitable timer: not available on this Windows");
    line("");

    line("--- the overlay's frame pacing (Sleep(1) + yield spin), timeBeginPeriod(1) active, " + std::to_string(secondsPerCase) + " s each");
    for (int hz : {60, 120, 240})
    {
        const double period = 1000.0 / hz;
        line(intervalstats::Row("frame pacing at " + std::to_string(hz) + " Hz (" + std::to_string(period).substr(0, 5) + " ms)", intervalstats::Summarize(PaceLoop(hz, secondsPerCase), period)));
    }
    timeEndPeriod(1);
    line("");
    line("Reading it: if 'Sleep(1), after timeBeginPeriod(1)' stays near 1-2 ms with a small p99, the overlay's pacing is fine. If it does not (Windows 10 2004+ / 11 may");
    line("ignore the request for a process without a visible window), the waitable-timer row shows the alternative. Send this file; do not change the pacing without it.");

    std::ofstream f("esp_pacing_overlay.txt", std::ios::binary);
    f << out;
    std::puts("written: esp_pacing_overlay.txt");
    return 0;
}
