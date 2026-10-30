#include "AgentSupervisor.h"
#include "Logger.h"

using namespace std::chrono_literals;

AgentSupervisor::AgentSupervisor(const AppOptions& opt, UdpListener& udp, AgentInjector& inj)
    : opt_(opt), udp_(udp), inj_(inj)
{
    pm_.SetSelector(procselect::Selector{opt.pid, opt.titleSelector, opt.exeSelector});
}

void AgentSupervisor::Tick()
{
    if (opt_.demo)
    {
        status_ = "demo";
        return;
    }
    const auto now = Clock::now();
    if (now - lastTick_ < 250ms) return;
    lastTick_ = now;

    if (!pm_.IsAttached())
    {
        status_ = pm_.Issue().empty() ? "waiting for minecraft" : pm_.Issue(); // e.g. "2 Minecraft clients found: ... choose one with --pid <PID>"
        if (now - lastAttach_ >= 1s)
        {
            lastAttach_ = now;
            if (pm_.Attach())
            {
                attachedAt_ = now;
                injectedPid_ = 0;
                inj_.Reset();
            }
            status_ = pm_.Issue().empty() ? "waiting for minecraft" : pm_.Issue();
        }
        return;
    }
    if (!pm_.IsAlive())
    {
        LOG_WARN("Minecraft process exited.");
        pm_.Detach();
        inj_.Reset();
        status_ = "minecraft closed - waiting for it to start again";
        return;
    }
    pm_.RefreshWindow();

    const auto last = udp_.LastPacketTime();
    if (last != Clock::time_point{} && now - last < 1500ms)
    {
        status_ = "agent connected";
        return;
    }

    // No (recent) data from the agent.
    const auto silentSince = (last != Clock::time_point{} && last > attachedAt_) ? last : attachedAt_;
    const auto state = inj_.GetState();
    if (state == AgentInjector::State::Running)
    {
        status_ = "attaching agent: " + inj_.Message();
        return;
    }

    if (opt_.autoInject && now - silentSince > 2s && (injectedPid_ != pm_.Target().pid || now - lastInject_ > 30s))
    {
        injectedPid_ = pm_.Target().pid;
        lastInject_ = now;
        inj_.Start(pm_.Target());
        status_ = "attaching agent...";
        return;
    }
    if (state == AgentInjector::State::Failed) status_ = "agent attach failed: " + inj_.Message();
    else if (state == AgentInjector::State::Succeeded) status_ =
        "agent attached, waiting for data (is a world loaded?)";
    else status_ = opt_.autoInject
                       ? "waiting for agent data"
                       : "waiting for agent (auto-inject off: start the game with -javaagent)";
}
