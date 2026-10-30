// AgentInjector.h - loads esp-agent.jar into the running Minecraft JVM (port of AgentInjector.cs).
//
// Attach workflow:
//   1. We start a short-lived helper JVM:  java [--add-modules jdk.attach] -cp esp-agent.jar mcradar.Attach <pid> <jar> <opts>
//      (JDK 8 needs tools.jar on the classpath instead; both cases are detected from the JRE's "release" file.)
//   2. mcradar.Attach uses the official JVM Attach API (com.sun.tools.attach.VirtualMachine - what jcmd,
//      VisualVM and IDE debuggers use) to ask the target JVM to load the jar as a Java agent.
//   3. The target JVM itself loads the jar and calls Agent.agentmain(); the agent's worker thread discovers
//      the player objects (see agent/src/mcradar/Probe.java) and streams snapshots to our UdpListener.
// Nothing is written into the game's memory by this tool.
//
// The helper JVM is taken from, in order: the GAME'S OWN JRE (same version, the C# tool's approach),
// --java <path>, %JAVA_HOME%, java.exe on PATH. Candidates that lack the Attach API are skipped.
#pragma once
#include "ProcessManager.h"
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

struct InjectorConfig
{
    std::filesystem::path jarPath; // esp-agent.jar
    std::string javaPath; // optional override
    uint16_t port = 47615;
    int agentHz = 120; // snapshots/second requested from the agent
    bool verbose = false; // pass verbose=1: the agent logs DEBUG lines too
    std::string token; // wire v5 session token handed to the agent (empty = none: the agent then writes v4)
    std::string extraOptions; // more ";key=value" agent options (AppOptions::agentExtraOptions): pacer, pacing report
};

class AgentInjector
{
public:
    enum class State { Idle, Running, Succeeded, Failed };

    explicit AgentInjector(InjectorConfig cfg) : cfg_(std::move(cfg))
    {
    }

    ~AgentInjector() { Join(); }
    AgentInjector(const AgentInjector&) = delete;
    AgentInjector& operator=(const AgentInjector&) = delete;

    void Start(const JavaProcess& target); // asynchronous; ignored while already running
    void Reset(); // Succeeded/Failed -> Idle
    State GetState() const { return state_.load(); }

    std::string Message() const
    {
        std::lock_guard<std::mutex> l(mtx_);
        return msg_;
    }

    int Progress() const { return progress_.load(); }

private:
    void Run(JavaProcess target, std::stop_token st);
    int RunHelper(const std::filesystem::path& java, DWORD pid, std::stop_token st, std::string& output);
    void Set(State s, const std::string& msg, int progress);

    void Join()
    {
        if (thread_.joinable())
        {
            thread_.request_stop();
            thread_.join();
        }
    }

    InjectorConfig cfg_;
    std::atomic<State> state_{State::Idle};
    std::atomic<int> progress_{0};
    mutable std::mutex mtx_;
    std::string msg_;
    std::jthread thread_;
};
