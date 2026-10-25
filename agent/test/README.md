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
