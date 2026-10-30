// SelfTestApp.h - mcesp --selftest: attach to the game, let the agent run its self-test, check the UDP path end to end, print one report, return its exit code.
#pragma once
#include "App.h"

int RunSelfTest(const AppOptions& opt);
