#!/bin/sh
# Compiles the agent (Java 8 bytecode, like agent/build.sh) and the Java-side tests, then runs them.
# Needs any JDK 8+ (CI runs it on 17, 21 and 25). Output goes to agent/out and agent/test/out (both git-ignored).
set -e
cd "$(dirname "$0")/.."
rm -rf out test/out && mkdir out test/out
javac --release 8 -Xlint:-options -d out src/mcradar/*.java
javac --release 8 -Xlint:-options -cp out -d test/out test/src/com/mojang/authlib/GameProfile.java test/src/it/unimi/dsi/fastutil/ints/*.java test/src/it/unimi/dsi/fastutil/objects/*.java test/src/mcradar/MiniJson.java test/src/mcradar/FixtureGame.java test/src/mcradar/FixtureTest.java test/src/mcradar/SnapshotTest.java test/src/mcradar/AgentConfigTest.java test/src/mcradar/LogTest.java test/src/mcradar/AgentReloadTest.java
SEP=:
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) SEP=";" ;; esac
java -cp "out${SEP}test/out" mcradar.SnapshotTest
java -cp "out${SEP}test/out" mcradar.AgentConfigTest
java -cp "out${SEP}test/out" mcradar.LogTest
java -cp "out${SEP}test/out" mcradar.AgentReloadTest
# structure fixtures: a fake game rebuilt from every test/fixtures/*/structure.json, discovery asserted against the dump (needs a JDK: it compiles the generated classes)
java -cp "out${SEP}test/out" mcradar.FixtureTest test/fixtures

# The C++ parser tests read real encoder output from ../tests/data/wire. It must be what the encoder produces today.
#   sh agent/test/run-java-tests.sh --update-corpus   rewrites it (do that on purpose when the wire format changes, and review the diff)
rm -rf test/out/corpus
java -cp "out${SEP}test/out" mcradar.SnapshotTest --dump test/out/corpus
if [ "$1" = "--update-corpus" ]; then
  mkdir -p ../tests/data/wire && cp test/out/corpus/*.dgram ../tests/data/wire/ && echo "corpus updated"
else
  # Compared after removing CRs and making sure there is one final newline: a corpus file that went through a line-ending conversion is still the same
  # datagram (the parser strips CRs). Anything else is a real difference, and the log shows what.
  norm() { tr -d '\r' < "$1" | sed -e '$a\'; }
  for f in test/out/corpus/*.dgram; do
    c="../tests/data/wire/$(basename "$f")"
    if [ ! -f "$c" ]; then echo "FAIL: $c is missing (run with --update-corpus)"; exit 1; fi
    norm "$f" > test/out/a.tmp; norm "$c" > test/out/b.tmp
    if ! cmp -s test/out/a.tmp test/out/b.tmp; then
      echo "FAIL: $c differs from what the encoder writes now (run with --update-corpus)"
      echo "  encoder: $(wc -c < "$f") bytes, committed: $(wc -c < "$c") bytes, first difference: $(cmp test/out/a.tmp test/out/b.tmp 2>&1 | head -1)"
      diff test/out/a.tmp test/out/b.tmp | head -10
      exit 1
    fi
  done
  echo "wire corpus matches the encoder"
fi
