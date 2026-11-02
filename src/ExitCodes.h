// ExitCodes.h - the process exit codes, in one place. Documented in docs/cli.md.
//   0        success
//   1        something failed while running (graphics could not start, an unexpected error)
//   2        wrong command line (unknown option, missing or bad value, unknown profile)
//   3..9     reserved (--selftest passes 0..3 through from the agent)
//   10..16   --selftest outcomes (docs/selftest.md); 16 = the UDP port is in use, here too
//   17       another mcesp is already running on this port
//   18       there is no usable agent jar (not embedded, not next to the exe, not given with --jar)
#pragma once

namespace exitcode
{
    inline constexpr int kOk = 0;
    inline constexpr int kRuntime = 1;
    inline constexpr int kUsage = 2;
    inline constexpr int kPortInUse = 16; // the same meaning as selftest::Report::kPortInUse
    inline constexpr int kAlreadyRunning = 17;
    inline constexpr int kNoAgentJar = 18;
} // namespace exitcode
