// SetupApp.h - the first-run checklist and `mcesp --setup`. Console only; the judgement is in SetupChecks.h.
#pragma once
#include "App.h"

// Gathers the facts, prints the checklist ([ OK ] / [WARN] / [FAIL] with what to do), and - when `interactive` - offers to copy a diagnostics report
// after a failure and to run the self-test (docs/selftest.md) when exactly one Minecraft is running. Returns exitcode::kOk, or kRuntime when something failed.
int RunSetup(const AppOptions& opt, bool interactive);

bool StdinIsConsole(); // an interactive session (not a pipe, not a double-click launcher without a console input)
