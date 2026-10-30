// AgentSupervisor.h - keeps "Minecraft is running" and "the agent is streaming" in sync.
//
//   no game          -> poll for a Minecraft JVM once per second
//   game, no data    -> listen ~2 s (the agent may already be there), then attach it via AgentInjector
//   game, data       -> nothing to do
//   game exited      -> forget the target; re-attach automatically when Minecraft starts again
// Cheap to call every frame; internally throttled to 4 Hz. Runs on the UI thread (the only slow part,
// the attach helper, is asynchronous inside AgentInjector).
#pragma once
#include "AgentInjector.h"
#include "InjectPolicy.h"
#include "App.h"
#include "ProcessManager.h"
#include "UdpListener.h"
#include <chrono>
#include <string>

class AgentSupervisor
{
public:
    using Clock = std::chrono::steady_clock;
    AgentSupervisor(const AppOptions& opt, UdpListener& udp, AgentInjector& inj);
    void Tick();
    bool HasGame() const { return pm_.IsAttached(); }
    HWND GameWindow() const { return pm_.Target().hwnd; }
    const std::string& Status() const { return status_; }
    // Incremented whenever the target changes: a game was found, or it exited. The overlay drops everything it holds from the old game when this moves.
    unsigned Generation() const { return generation_; }

private:
    InjectPolicy policy_;
    unsigned generation_ = 0;
    bool outcomeCounted_ = true; // the current injection's result was fed to the policy
    const AppOptions& opt_;
    UdpListener& udp_;
    AgentInjector& inj_;
    ProcessManager pm_;
    std::string status_ = "waiting for minecraft";
    Clock::time_point lastTick_{}, lastAttach_{}, attachedAt_{}, lastInject_{};
    DWORD injectedPid_ = 0;
};
