#!/bin/sh
# Regenerates the synthetic recording tests/data/synthetic_walk.esprec (fixed seed). The committed file is what the tests read; they never
# regenerate it. Regenerating on another compiler/libm may change a few low bits of the text, so only do it on purpose, then run
# tests/regen-goldens.sh and review both diffs.
set -e
cd "$(dirname "$0")/.."
B=build/tests
cmake -S tests -B "$B" -DCMAKE_BUILD_TYPE=Debug >/dev/null
cmake --build "$B" -j "$(nproc 2>/dev/null || echo 4)" --target gen_recording
"$B/gen_recording" --seconds 12 --seed 1 tests/data/synthetic_walk.esprec
