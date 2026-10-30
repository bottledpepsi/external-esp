# Agent test harness (no Minecraft needed)

`FakeGame` imitates Minecraft's *structure* with obfuscated names (a Vec3-like class, a position
field next to a velocity field, a tab-list class that has a GameProfile but no position, a mixed
"all entities" list, a server-side player list, and an old-style variant with plain double fields).

```
cd agent/test
javac -d out src/com/mojang/authlib/GameProfile.java src/FakeGame.java
java -javaagent:../esp-agent.jar="port=47615;export=./exp" -cp out FakeGame --username Alice      # new style
java -javaagent:../esp-agent.jar="port=47615;export=./exp" -cp out FakeGame --username Alice old  # old style
```
Then run `mcradar --agent` (or any UDP listener on 127.0.0.1:47615) and check `exp/mcradar_layout.json`.
Expected: Alice is the local player, Bob and Carol are the other players, yaw values follow the fake look vector.

Extra flags: `wiggle` sweeps the local head so the agent can identify the camera; `third` puts the camera 4 blocks behind the eye.
The fake world also contains an NBT-style enchanted sword, a component-style enchanted book, cobblestone x37, an item frame (decoy, 0.75 hitbox), a mob,
a camera object with a private `getFov(Camera, float, boolean)` method whose result sweeps 63-77, and AABB bounding boxes (one player sneaking).

## RealisticGame (real 1.21.x structure)
`FakeGame` gives its item entity a direct stack field, which real Minecraft does not. `RealisticGame` reproduces the real quirks:
`ItemEntity` without a stack field (stack in `entityData -> DataItem[] -> value`, `initialValue` = empty stack), `ItemStack` = holder + count + components + entity back-reference,
entities in `ClientLevel -> TransientEntityManager -> EntityLookup -> fastutil-style map` below a big decoy graph, and a map mutated while the agent reads it.
```
cd agent/test
javac -d /tmp/rg src/com/mojang/authlib/GameProfile.java src/it/unimi/dsi/fastutil/ints/*.java src/it/unimi/dsi/fastutil/objects/*.java src/RealisticGame.java
java -javaagent:../esp-agent.jar="port=47615;export=/tmp/exp;user=Alice" -cp /tmp/rg RealisticGame            # expects 5 items
java ... RealisticGame nokey      # ItemStack without a key method: id resolved through the holder
java ... RealisticGame hidden     # layout the search cannot see: expects 0 items and an esp_items_debug.txt
```

## Unit tests (no game, no JUnit)
`sh agent/test/run-java-tests.sh` compiles the agent and `test/src/mcradar/SnapshotTest.java` (package `mcradar`, so it can see
the package-private encoder) and runs it. It checks the wire layout, locale independence (decimal comma, non-ASCII digits),
percent encoding, NaN/Infinity -> `-`, name/status sanitising and the datagram size cap. Needs any JDK 8+.

## Smoke test (agent -> UDP, no Minecraft)
`python3 agent/test/agent_smoke.py [--java-home DIR] [--timeout S] [fake|fake-old|fake-third|realistic ...]` builds the agent jar and the fake games
into `agent/test/out-smoke/`, launches each game with `-javaagent`, listens on a free loopback port and checks the datagrams:
header `MCRADAR 4`, non-decreasing `T` stamps, Alice local + Bob/Carol, Carol's sneaking hitbox (1.50), camera FOV in the fake 63-77 sweep and
the right view mode, exactly the three real items (the item frame is filtered; the sword's `sharpness@5` is parsed), five items for `RealisticGame`.
It stops a case as soon as everything is satisfied (about 2 s) or fails after `--timeout` (45 s). Needs a JDK 8+ and Python 3.8+.

## Wire corpus (Java -> C++ contract)
`run-java-tests.sh` also runs `SnapshotTest --dump` and compares the datagrams with `tests/data/wire/*.dgram` (a full snapshot, an empty one, unicode names,
a crowd of 40 players and 60 items). The C++ parser tests read exactly those files, so an encoder change that the parser does not understand
fails a test instead of a game session. After an intentional wire change: `sh agent/test/run-java-tests.sh --update-corpus` and review the diff.

## Reload (overlay restarted while the game keeps running)
`AgentReloadTest` (in `run-java-tests.sh`) runs the real `Agent` in-process with a fake `Instrumentation` and loads it several times with different
port / rate / token; `agent_smoke.py reload` does the same with a real Attach into `FakeGame` (needs JDK 9+; skipped on 8).

## Discovery regressions (`DiscoveryRegressionTest`)
Driven through `FixtureGame` (docs/fixtures.md), which can make the fake game misbehave: a NaN look vector (a half-initialised player), a NaN camera yaw,
a player looking straight up (pitch -90: no horizontal look vector, so the yaw is held). Asserted: nothing throws, no `NaN`/`Infinity` reaches the wire,
the camera scanner is not trained on unknown or held yaw (900 ticks of held yaw would push the mean yaw error to ~33 degrees, over the scanner's 20 degree
limit - the test fails if that guard in `Probe.read` is removed; checked by hand with that mutation).
