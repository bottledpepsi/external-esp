// Version.h - the build id shown in the diagnostics report. CMake sets ESP_VERSION from `git describe --always --dirty` when it CONFIGURES
// (re-run cmake to refresh it); builds without it (build-all.bat, a hand-written g++ line) report "unknown".
#pragma once

#ifndef ESP_VERSION
#define ESP_VERSION "unknown"
#endif

inline constexpr const char* kEspVersion = ESP_VERSION;
