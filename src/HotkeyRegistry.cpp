#include "HotkeyRegistry.h"

namespace hotkeys
{
    std::string Registry::Describe(const Binding& b, int error)
    {
        const std::string k = Format(b);
        if (error == kErrAlreadyRegistered) return k + ": taken by another program, polled instead";
        if (error == kErrInvalidParameter) return k + ": not allowed as a system hotkey, polled instead";
        return k + ": Windows error " + std::to_string(error) + ", polled instead";
    }

    void Registry::Release(Action a)
    {
        State& s = st_[idx(a)];
        if (s.registered) reg_.Unregister(static_cast<int>(a));
        s.registered = false;
        s.bound = Binding{};
    }

    void Registry::Sync(const Table& t, bool active, Clock::time_point now)
    {
        for (int i = 0; i < kActionCount; ++i)
        {
            const Action a = ActionAt(i);
            State& s = st_[idx(a)];
            const Binding& want = t[idx(a)];
            const bool eligible = active && want.Bound() && !Def(a).hold && Problem(want).empty();
            if (!eligible)
            {
                Release(a);
                s.failed = Binding{};
                s.error = 0;
                s.reported = false;
                // a binding Windows keeps for itself is explained even though nothing was tried
                s.status = (active && want.Bound() && !Def(a).hold) ? Format(want) + ": reserved by Windows, not registered" : std::string();
                continue;
            }
            if (s.registered && s.bound == want) continue; // already as wanted
            Release(a); // registered with a different binding (rebind)
            if (s.failed == want && now < s.retryAt) continue; // tried and failed a moment ago: stay on polling for now
            const int err = reg_.Register(static_cast<int>(a), want);
            if (err == 0)
            {
                s.registered = true;
                s.bound = want;
                s.failed = Binding{};
                s.error = 0;
                s.status.clear();
                s.reported = false;
            }
            else
            {
                s.failed = want;
                s.error = err;
                s.retryAt = now + kRetryAfter;
                s.status = Describe(want, err);
                if (!s.reported)
                {
                    s.reported = true;
                    fresh_.push_back({a, want, err, s.status});
                }
            }
        }
    }

    void Registry::Shutdown()
    {
        for (int i = 0; i < kActionCount; ++i) Release(ActionAt(i));
    }

    bool Registry::NeedsPolling(const Table& t, Action a) const
    {
        const Binding& b = t[idx(a)];
        if (!b.Bound()) return false;
        return Def(a).hold || !st_[idx(a)].registered;
    }

    std::vector<Registry::Failure> Registry::TakeNewFailures()
    {
        std::vector<Failure> out;
        out.swap(fresh_);
        return out;
    }
} // namespace hotkeys
