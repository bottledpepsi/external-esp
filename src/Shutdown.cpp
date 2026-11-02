#include "Shutdown.h"
#include <algorithm>
#include <cstring>

namespace exitflow
{
    const char* Name(Reason r)
    {
        switch (r)
        {
        case Reason::Hotkey: return "the exit hotkey was held";
        case Reason::WindowClosed: return "the overlay window was closed";
        case Reason::CtrlC: return "Ctrl+C";
        case Reason::ConsoleClosed: return "the console window was closed";
        case Reason::LogOff: return "Windows is logging off or shutting down";
        case Reason::Error: return "a fatal error";
        default: return "(no reason)";
        }
    }

    ExitRequest& Global()
    {
        static ExitRequest g;
        return g;
    }

    void Sequence::Add(const std::string& name, std::function<void()> fn, std::chrono::milliseconds budget)
    {
        steps_.push_back({name, std::move(fn), budget});
    }

    std::vector<StepResult> Sequence::Run()
    {
        std::vector<Step*> order;
        for (const char* n : kOrder)
            for (Step& s : steps_)
                if (s.name == n) order.push_back(&s);
        for (Step& s : steps_)
            if (std::find(order.begin(), order.end(), &s) == order.end()) order.push_back(&s); // unknown name: still run it, last

        std::vector<StepResult> out;
        for (Step* s : order)
        {
            StepResult r;
            r.name = s->name;
            const auto t0 = Clock::now();
            try
            {
                if (s->fn) s->fn();
            }
            catch (const std::exception& e)
            {
                r.ok = false;
                r.error = e.what();
            }
            catch (...)
            {
                r.ok = false;
                r.error = "unknown error";
            }
            r.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0);
            r.overBudget = r.elapsed > s->budget;
            out.push_back(std::move(r));
        }
        return out;
    }

    std::string Sequence::Summary(const std::vector<StepResult>& r)
    {
        std::string s;
        for (const StepResult& x : r)
        {
            if (!s.empty()) s += ", ";
            s += x.name + " " + std::to_string(x.elapsed.count()) + " ms";
            if (!x.ok) s += " (FAILED: " + x.error + ")";
            else if (x.overBudget) s += " (slow)";
        }
        return s;
    }
} // namespace exitflow
