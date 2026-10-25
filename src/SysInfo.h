// SysInfo.h - facts about the machine for the diagnostics report: OS, CPU, user and computer name, folders to redact.
#pragma once
#include <string>

namespace sysinfo
{
    struct Info
    {
        std::string os, cpu, userName, hostName, userProfile;
    };

    Info Collect();
    std::string CompilerName(); // "GCC 15.2.0" / "Clang ..." / "MSVC ..."
} // namespace sysinfo
