// AgentJarWin.h - which agent jar to use, and where it lives. Windows only.
//   1. --jar PATH                      (you said so)
//   2. the jar embedded in this exe, extracted to %LOCALAPPDATA%\external-esp\agent\ (verified by hash; see AgentJar.h)
//   3. esp-agent.jar next to the exe   (a build without the embedded resource, or an embedded one that could not be written)
// The embedded jar wins over a stale esp-agent.jar left next to a newer exe on purpose: the two always belong together.
#pragma once
#include "AgentJar.h"

namespace agentjar
{
    bool EmbeddedBlob(Blob& out); // the resource section's copy, if this exe has one
    std::filesystem::path CacheDir(); // %LOCALAPPDATA%\external-esp\agent

    struct Resolved
    {
        enum class Source { CommandLine, Embedded, BesideExe };
        std::filesystem::path jar;
        std::filesystem::path exportDir; // where the agent's log / the self-test report go
        Source source = Source::BesideExe;
        Extracted::How how = Extracted::How::Reused;
        std::string hash;
        int removedOld = 0;
        std::string problem; // an embedded jar that could not be extracted: why
    };
    Resolved Resolve(const std::filesystem::path& explicitJar, const std::filesystem::path& besideExe);
} // namespace agentjar
