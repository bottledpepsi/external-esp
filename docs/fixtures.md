# Structure dumps and version fixtures

The agent finds players, the camera, the FOV and dropped items by **structure**, not by Minecraft names. So "it does not work on version X" can only be
debugged if the structure of X is known. `esp_structure.json` records exactly that.

## Getting a dump from a real game
Start the overlay as usual, join a world, look around a little and drop an item, then send `esp_structure.json` (it is written next to `esp_layout.json`, which is
the agent's export folder = the folder of `mcesp.exe`). Turn the dump on with the agent option `dump=1`:

* automatic attach: `mcesp --dump-structure` (added with the self-test patch; until then use `--no-inject`)
* `--no-inject`: add `;dump=1` to the `-javaagent:` line that `mcesp` prints.

The file is rewritten (at most every 2 s) whenever the agent learns something new (the camera was decided, a new item class was seen).

## What is in it, and what is not
Classes along the discovered paths (player list, local player, camera and its renderer, entity container, item entity -> stack -> item id) with their declared
fields (name, type, generic type, static), the interfaces and super classes, the methods the agent actually uses (look vector, health, FOV, item id) and the
paths themselves. **No values**: no player names, UUIDs, coordinates, tokens, chat, server addresses or file paths - `agent_smoke.py fake-dump` fails if a
name or token ever appears in it. It does not contain the rest of the game (decoy object graphs, thousands of other classes), so a game rebuilt from it is
easier for the agent than the real one; a fixture that passes is necessary, not sufficient.

`game.minecraft` comes from `version.json` in the game's jar, else from `--version` on the JVM command line, else `unknown`.

## Fixtures: testing discovery without Minecraft
`agent/test/fixtures/NAME/structure.json` (a dump) + `fixture.txt` (`origin:` and `real: true|false`). `sh agent/test/run-java-tests.sh` (CI runs it) rebuilds a fake
game from every fixture (`FixtureGame`: the dumped classes, fields, generic types and super classes are generated as Java source, compiled in memory with
the JDK's compiler and wired into an object graph along the dumped paths), runs the agent's real `Probe` against it and asserts that discovery finds what
the dump says it found in the real game: player list path, local player path, camera path / yaw / pitch, the FOV method, a stack path for every item entity
class, and that the snapshot has two other players, a camera, and every item with a resolved id.

**What a passing fixture means.** `real: false` fixtures are dumps of this repo's own fake games: they prove the round trip works and guard against
regressions, nothing about a Minecraft version. The summary line counts them separately ("0 real, 2 synthetic passed"). Only a dump with `real: true` can
support "version X is covered", and even then only as far as the dump goes (see the limits above: no decoy graphs, invented values, `final` dropped).

**Adding a real version** (about two minutes):
1. get `esp_structure.json` from that version (see above); read it once - it holds no names or values;
2. `mkdir agent/test/fixtures/1.21.1-vanilla` (the version and the loader), copy it there as `structure.json`;
3. write `fixture.txt`: `origin: <who dumped it, which launcher/loader, date>` and `real: true`;
4. `sh agent/test/run-java-tests.sh`. A fixture that fails is the bug report: `FixtureTest` names the path or accessor that differs.
A dump taken where discovery already failed has no `camera.decided`, no items or no paths: the test then only asserts what the dump contains, which is
exactly the information needed to see why the game is not understood.

`java -cp agent/out:agent/test/out mcradar.FixtureTest agent/test/fixtures [name ...]` runs a subset.
