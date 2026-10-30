// PacingTestApp.h - mcesp --pacing-test: how precisely can THIS process wait on THIS Windows? Prints a table and writes esp_pacing_overlay.txt.
#pragma once
#include "App.h"

int RunPacingTest(const AppOptions& opt, int secondsPerCase);
