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
