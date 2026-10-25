#!/bin/sh
# Regenerates tests/data/<name>.golden.json from tests/data/<name>.esprec. Do this ON PURPOSE, after a change that is supposed to move the
# jitter numbers (a smoother or camera change), then READ the diff: a golden that got worse is a regression, not a new baseline.
#   tests/regen-goldens.sh            all recordings
#   tests/regen-goldens.sh walk       just tests/data/walk.esprec
set -e
cd "$(dirname "$0")/.."
B=build/tests
cmake -S tests -B "$B" -DCMAKE_BUILD_TYPE=Debug >/dev/null
cmake --build "$B" -j "$(nproc 2>/dev/null || echo 4)" --target golden_runner
if [ $# -gt 0 ]; then set -- $(for n in "$@"; do echo "tests/data/$n.esprec"; done); else set -- tests/data/*.esprec; fi
for rec in "$@"; do
  [ -f "$rec" ] || { echo "no recording $rec"; exit 1; }
  gold="${rec%.esprec}.golden.json"
  echo "== $rec"
  (cd "$(dirname "$B")/.." && "$B/golden_runner" --update "$rec" "$gold")
done
git --no-pager diff --stat -- tests/data 2>/dev/null || true
