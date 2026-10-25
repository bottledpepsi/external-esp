#!/bin/sh
# Compiles the agent (Java 8 bytecode, like agent/build.sh) and the Java-side tests, then runs them.
# Needs any JDK 8+ (CI runs it on 17, 21 and 25). Output goes to agent/out and agent/test/out (both git-ignored).
set -e
cd "$(dirname "$0")/.."
rm -rf out test/out && mkdir out test/out
javac --release 8 -Xlint:-options -d out src/mcradar/*.java
javac --release 8 -Xlint:-options -cp out -d test/out test/src/mcradar/SnapshotTest.java test/src/mcradar/AgentConfigTest.java test/src/mcradar/LogTest.java
SEP=:
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) SEP=";" ;; esac
java -cp "out${SEP}test/out" mcradar.SnapshotTest
java -cp "out${SEP}test/out" mcradar.AgentConfigTest
java -cp "out${SEP}test/out" mcradar.LogTest

# The C++ parser tests read real encoder output from ../tests/data/wire. It must be what the encoder produces today.
#   sh agent/test/run-java-tests.sh --update-corpus   rewrites it (do that on purpose when the wire format changes, and review the diff)
rm -rf test/out/corpus
java -cp "out${SEP}test/out" mcradar.SnapshotTest --dump test/out/corpus
if [ "$1" = "--update-corpus" ]; then
  mkdir -p ../tests/data/wire && cp test/out/corpus/*.dgram ../tests/data/wire/ && echo "corpus updated"
else
  for f in test/out/corpus/*.dgram; do
    cmp -s "$f" "../tests/data/wire/$(basename "$f")" || { echo "FAIL: tests/data/wire/$(basename "$f") differs from what the encoder writes now (run with --update-corpus)"; exit 1; }
  done
  echo "wire corpus matches the encoder"
fi
