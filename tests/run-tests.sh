#!/bin/sh
# Host-side unit tests (Linux/macOS/MSYS2): config <-> JSON, C# config compatibility, friends, profiles,
# wire parser, camera projection (orientation at 4 yaws, pitch, FOV edge), smoother. Needs cmake + a C++20 compiler.
# Extra arguments are passed to ctest, e.g.  tests/run-tests.sh -R unit_tests
set -e
cd "$(dirname "$0")/.."
B=build/tests
cmake -S tests -B "$B" -DCMAKE_BUILD_TYPE=Debug >/dev/null
cmake --build "$B" -j "$(nproc 2>/dev/null || echo 4)"
ctest --test-dir "$B" --output-on-failure "$@"
