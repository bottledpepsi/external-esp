// single_instance_test - names (portable part) and the exit-code table. The mutex itself is exercised by tests/windows/single_instance_probe.cpp.
#include "ExitCodes.h"
#include "SelfTestReport.h"
#include "SingleInstance.h"
#include <cstdio>
#include <set>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

int main()
{
    CHECK(SingleInstance::MutexName(47615) == "Local\\external-esp-overlay-47615");
    CHECK(SingleInstance::MutexName(47615) == SingleInstance::MutexName(47615));
    CHECK(SingleInstance::MutexName(47615) != SingleInstance::MutexName(47616));   // two clients on one PC: two ports, no conflict
    CHECK(SingleInstance::MutexName(1).rfind("Local\\", 0) == 0);                  // per logon session
    CHECK(SingleInstance::WindowTitle(47616) == L"external-esp :47616");

    // the general exit codes never collide with each other or with the self-test outcomes, except where the meaning is the same
    using namespace selftest;
    const std::set<int> selftestCodes = {kOk, kSkips, kWarn, kFail, kNotMinecraft, kNotInWorld, kNoData, kAmbiguous, kAttachFailed, kNoReport, kPortInUse};
    CHECK(exitcode::kPortInUse == selftest::kPortInUse);
    CHECK(!selftestCodes.count(exitcode::kAlreadyRunning) && !selftestCodes.count(exitcode::kNoAgentJar));
    CHECK(exitcode::kAlreadyRunning != exitcode::kNoAgentJar);
    CHECK(exitcode::kOk == 0 && exitcode::kRuntime == 1 && exitcode::kUsage == 2);

    if (fails) std::printf("%d failure(s)\n", fails); else std::puts("single_instance_test: all passed");
    return fails ? 1 : 0;
}
