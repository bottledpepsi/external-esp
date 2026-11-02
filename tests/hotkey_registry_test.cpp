// hotkey_registry_test - which hotkeys are registered with the system when: focus gating, rebinding, conflicts with other programs, retry back-off.
#include "HotkeyRegistry.h"
#include <cstdio>
#include <map>
#include <set>
using namespace hotkeys;
using namespace std::chrono_literals;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

struct Fake : Registrar
{
    std::map<int, Binding> live;          // what is registered right now
    std::set<std::pair<unsigned, int>> takenByOthers; // (mods, vk) that another program owns
    int registerCalls = 0, unregisterCalls = 0;
    int Register(int id, const Binding& b) override
    {
        ++registerCalls;
        if (takenByOthers.count({b.mods, b.vk})) return kErrAlreadyRegistered;
        for (const auto& kv : live) if (kv.second == b) return kErrAlreadyRegistered; // the system allows a combination once
        live[id] = b;
        return 0;
    }
    void Unregister(int id) override { ++unregisterCalls; live.erase(id); }
};
static Binding P(const char* s) { Binding b; Parse(s, b); return b; }
static const Registry::Clock::time_point T0 = Registry::Clock::now();

int main()
{
    // ---- not active: nothing is registered (no key is taken from anyone) ----
    {
        Fake f; Registry r(f);
        r.Sync(Defaults(), false, T0);
        CHECK(f.live.empty() && f.registerCalls == 0);
    }
    // ---- active: the bound, non-hold actions; never HoldExit or unbound ones ----
    {
        Fake f; Registry r(f);
        r.Sync(Defaults(), true, T0);
        CHECK(f.live.size() == 2 && f.live.count(0) && f.live.count(1));
        CHECK(f.live[0] == P("Delete") && f.live[1] == P("Insert"));
        CHECK(!r.IsRegistered(Action::HoldExit) && r.NeedsPolling(Defaults(), Action::HoldExit));     // held actions are polled
        CHECK(!r.NeedsPolling(Defaults(), Action::ToggleOverlay) && !r.NeedsPolling(Defaults(), Action::TogglePlayers)); // unbound: nothing to poll
        // modifiers reach the system as bound
        Table t = Defaults();
        t[3] = P("Ctrl+Shift+P");
        r.Sync(t, true, T0 + 1s);
        CHECK(f.live[3].mods == (kCtrl | kShift) && f.live[3].vk == 'P');
        // idempotent
        const int calls = f.registerCalls;
        r.Sync(t, true, T0 + 2s); r.Sync(t, true, T0 + 3s);
        CHECK(f.registerCalls == calls && f.unregisterCalls == 0);
    }
    // ---- focus lost: everything is released at once; regained: registered again ----
    {
        Fake f; Registry r(f);
        r.Sync(Defaults(), true, T0);
        CHECK(f.live.size() == 2);
        r.Sync(Defaults(), false, T0 + 1s);
        CHECK(f.live.empty() && !r.IsRegistered(Action::ToggleOverlay));
        r.Sync(Defaults(), true, T0 + 2s);
        CHECK(f.live.size() == 2);
        r.Shutdown(); r.Shutdown();
        CHECK(f.live.empty());
    }
    // ---- rebind and unbind while active ----
    {
        Fake f; Registry r(f);
        Table t = Defaults();
        r.Sync(t, true, T0);
        t[0] = P("F6");
        r.Sync(t, true, T0 + 1s);
        CHECK(f.live[0] == P("F6") && f.live.size() == 2);
        t[3] = P("F7");
        r.Sync(t, true, T0 + 2s);
        CHECK(f.live.size() == 3);
        t[3] = Binding{};
        r.Sync(t, true, T0 + 3s);
        CHECK(f.live.size() == 2 && !r.IsRegistered(Action::TogglePlayers));
    }
    // ---- a key another program owns: clear message, polling fallback, reported once, retried after the back-off ----
    {
        Fake f; Registry r(f);
        f.takenByOthers.insert({0u, 0x2E}); // Delete belongs to someone else
        r.Sync(Defaults(), true, T0);
        CHECK(!r.IsRegistered(Action::ToggleOverlay) && r.IsRegistered(Action::OpenConfig));
        CHECK(r.NeedsPolling(Defaults(), Action::ToggleOverlay));
        CHECK(r.Status(Action::ToggleOverlay) == "Delete: taken by another program, polled instead");
        CHECK(r.Status(Action::OpenConfig).empty());
        auto fresh = r.TakeNewFailures();
        CHECK(fresh.size() == 1 && fresh[0].error == kErrAlreadyRegistered && fresh[0].action == Action::ToggleOverlay);
        CHECK(r.TakeNewFailures().empty());
        const int calls = f.registerCalls;
        r.Sync(Defaults(), true, T0 + 1s); r.Sync(Defaults(), true, T0 + 4900ms);
        CHECK(f.registerCalls == calls);                                    // not hammering RegisterHotKey every frame
        r.Sync(Defaults(), true, T0 + 5s);
        CHECK(f.registerCalls == calls + 1 && r.TakeNewFailures().empty()); // retried, still failing, not logged again
        f.takenByOthers.clear();                                            // the other program let go
        r.Sync(Defaults(), true, T0 + 10s);
        CHECK(r.IsRegistered(Action::ToggleOverlay) && r.Status(Action::ToggleOverlay).empty() && !r.NeedsPolling(Defaults(), Action::ToggleOverlay));
        // focus out and in again: the other program may have taken it meanwhile -> a fresh try, and a fresh message
        f.takenByOthers.insert({0u, 0x2E});
        r.Sync(Defaults(), false, T0 + 11s);
        r.Sync(Defaults(), true, T0 + 12s);
        CHECK(!r.IsRegistered(Action::ToggleOverlay) && r.TakeNewFailures().size() == 1);
    }
    // ---- a combination Windows keeps: not tried, explained ----
    {
        Fake f; Registry r(f);
        Table t = Defaults();
        t[3] = P("Alt+F4");
        r.Sync(t, true, T0);
        CHECK(!r.IsRegistered(Action::TogglePlayers) && r.Status(Action::TogglePlayers).find("reserved by Windows") != std::string::npos);
        CHECK(f.live.count(3) == 0);
    }
    // ---- two actions on one combination (the file checks prevent it, but the registry must cope): the second one polls ----
    {
        Fake f; Registry r(f);
        Table t = Defaults();
        t[3] = P("F6"); t[4] = P("F6");
        r.Sync(t, true, T0);
        CHECK(r.IsRegistered(Action::TogglePlayers) && !r.IsRegistered(Action::ToggleItems) && r.NeedsPolling(t, Action::ToggleItems));
    }
    // ---- the destructor releases everything ----
    {
        Fake f;
        {
            Registry r(f);
            r.Sync(Defaults(), true, T0);
            CHECK(f.live.size() == 2);
        }
        CHECK(f.live.empty());
    }
    // ---- every status text fits one line of the window ----
    for (int e : {kErrAlreadyRegistered, kErrInvalidParameter, 5}) CHECK(Registry::Describe(P("Ctrl+Shift+Alt+F12"), e).size() <= 72);

    // ---- error texts ----
    CHECK(Registry::Describe(P("F6"), kErrInvalidParameter).find("not allowed as a system hotkey") != std::string::npos);
    CHECK(Registry::Describe(P("Ctrl+F6"), 5).find("Windows error 5") != std::string::npos);

    if (fails) std::printf("%d failure(s)\n", fails); else std::puts("hotkey_registry_test: all passed");
    return fails ? 1 : 0;
}
