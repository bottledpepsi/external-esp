// HotkeyRegistry.h - decides which hotkeys are registered with the system (RegisterHotKey) at any moment. Portable: the Win32 calls sit behind a
// small Registrar interface, so the rules are tested with a fake.
//
// Why the rules matter: a registered system hotkey is taken away from EVERY program (the game does not see Delete or Ctrl+Shift+P any more, and
// neither does a browser in the foreground). So:
//   * keys are registered only while the game (or the overlay window) has focus and nothing is waiting for a key (a rebind), and are released the
//     moment that stops being true;
//   * a key that another program already owns is not an error for the user: the registration fails with a clear message and that action falls
//     back to polling (the key then also reaches the game, as before this feature);
//   * actions that need "held for 3 s" (HoldExit) are never registered; they are polled.
#pragma once
#include "Hotkeys.h"
#include <array>
#include <chrono>
#include <string>
#include <vector>

namespace hotkeys
{
    class Registrar
    {
    public:
        virtual ~Registrar() = default;
        // Returns 0 on success, else the system's error code (1409 = already registered by someone else).
        virtual int Register(int id, const Binding& b) = 0;
        virtual void Unregister(int id) = 0;
    };

    inline constexpr int kErrAlreadyRegistered = 1409; // ERROR_HOTKEY_ALREADY_REGISTERED
    inline constexpr int kErrInvalidParameter = 87; // ERROR_INVALID_PARAMETER

    class Registry
    {
    public:
        using Clock = std::chrono::steady_clock;
        static constexpr std::chrono::seconds kRetryAfter{5};

        explicit Registry(Registrar& r) : reg_(r) {}
        ~Registry() { Shutdown(); }
        Registry(const Registry&) = delete;
        Registry& operator=(const Registry&) = delete;

        // Call every loop pass. `active` = the game or overlay has focus and no key capture is pending.
        void Sync(const Table& t, bool active, Clock::time_point now);
        void Shutdown(); // unregisters everything; safe to call twice

        bool IsRegistered(Action a) const { return st_[idx(a)].registered; }
        // true when this action has to be polled: it is bound and either held-type or its registration did not work
        bool NeedsPolling(const Table& t, Action a) const;
        // "" when fine, else one sentence for the HOTKEYS tab ("taken by another program ...")
        const std::string& Status(Action a) const { return st_[idx(a)].status; }

        struct Failure
        {
            Action action;
            Binding binding;
            int error;
            std::string text;
        };
        std::vector<Failure> TakeNewFailures(); // each failed (action, binding, error) once, for the log

        static std::string Describe(const Binding& b, int error);

    private:
        struct State
        {
            bool registered = false;
            Binding bound{}; // what is registered
            Binding failed{}; // what failed last (so a retry waits)
            int error = 0;
            Clock::time_point retryAt{};
            std::string status;
            bool reported = false;
        };
        static size_t idx(Action a) { return static_cast<size_t>(a); }
        void Release(Action a);

        Registrar& reg_;
        std::array<State, kActionCount> st_{};
        std::vector<Failure> fresh_;
    };
} // namespace hotkeys
