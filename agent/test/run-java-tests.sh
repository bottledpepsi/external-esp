#!/bin/sh
# Compiles the agent (Java 8 bytecode, like agent/build.sh) and the Java-side tests, then runs them.
# Needs any JDK 8+ (CI runs it on 17, 21 and 25). Output goes to agent/out and agent/test/out (both git-ignored).
set -e
cd "$(dirname "$0")/.."
rm -rf out test/out && mkdir out test/out
javac --release 8 -Xlint:-options -d out src/mcradar/*.java
javac --release 8 -Xlint:-options -cp out -d test/out test/src/mcradar/SnapshotTest.java
SEP=:
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) SEP=";" ;; esac
java -cp "out${SEP}test/out" mcradar.SnapshotTest
