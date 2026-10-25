# Wire format (agent -> overlay)

The agent sends one UDP datagram (to `127.0.0.1:47615`) about 120 times a second. It is plain UTF-8 text, one record per line. The producer
is `agent/src/mcradar/Snapshot.java` (`encode`), the consumer is `src/Wire.cpp` (`ParseDatagram`). `tests/data/wire/*.dgram` are real encoder
outputs that the C++ tests parse (see `agent/test/README.md`).

```
MCRADAR 4                                                            header: protocol version (1-4 accepted)
T <nanoTime>                                                         v4: instant the agent read the game state (agent clock, ns)
S <status text>                                                      status line shown in the HUD
C <x> <y> <z> <yaw> <pitch> <fov|-> <mode>                           v3+: the game camera; mode 1 first / 2 third-back / 3 third-front
L <name> <x> <y> <z> <yaw|-> <pitch|-> <health|-> <w|-> <h|-> <uuid|->    the local player
P <name> <same columns as L>                                         every other player
F 1                                                                  v4: an item list follows (absent = keep the previous list)
I <x> <y> <z> <w|-> <h|-> <count> <key> <ench|-> <tags|->            v3+: a dropped item; key/ench/tags are percent-encoded
```

`-` means "unknown". Columns are separated by spaces or tabs, lines by `\n` (a trailing `\r` is ignored), unknown tags and lines shorter
than two characters are skipped. Versions 1 (yaw only), 2 (+ pitch, health) and 3 are still parsed.

## The parser treats the datagram as untrusted input

Any process on the machine can send a datagram to the port, and a bug in the agent can send nonsense, so `ParseDatagram` validates everything
(constants in `src/Wire.h`, namespace `wire`):

| What | Rule |
|---|---|
| Header | exactly `MCRADAR <1-3 digits>` then end of line or a space. `MCRADAR 10` and `MCRADAR 4junk` are not ours (they used to be read as v1 and v4). A version this parser does not know (> 4) is rejected |
| Numbers | plain decimals only (`-`, digits, `.`, `e`/`E`): no `nan`, `inf`, hex floats, `+`, no 1e999. Must be finite |
| Coordinates | `abs(v) <= 1e8` (the world border is 3e7). A line with a bad coordinate is dropped |
| Angles | `abs(v) <= 1e6` degrees; otherwise the angle is "unknown" |
| Health | `0 .. 1e5`; otherwise unknown |
| Box width/height | `> 0 and <= 64`; otherwise the size is unknown |
| FOV | `1 .. 179`; otherwise unknown (the configured FOV is used) |
| Camera mode | integer 1-3; anything else is 1 |
| Item count | clamped to `1 .. 1 000 000` before it is converted to `int` (a `nan` or `1e30` there was undefined behaviour) |
| `T` stamp | a whole signed 64-bit integer; otherwise ignored |
| Per datagram | at most 256 `P` lines and 1024 `I` lines are kept (the rest are counted in `ignoredLines`), 32 enchantments per item |
| Text | names 64 bytes, uuid 40, status 256, item key 128, enchant text 1024, tags 4096. Text is forced to valid UTF-8 (each bad byte becomes U+FFFD, so overlong forms, surrogates and values above U+10FFFF cannot get through) and control characters become `?`. Percent escapes are decoded first, so `%00` or `%FF` cannot smuggle a NUL or invalid byte in. Truncation never splits a character |

`WorldSnapshot::ignoredLines` counts lines with a known tag that were dropped. A rejected datagram leaves the output snapshot untouched.

Tests: `tests/wire_validation_test.cpp` (each rule), `tests/wire_fuzz_test.cpp` (hostile corpus + seeded mutation fuzzer; every returned
snapshot must satisfy `tests/wire_invariants.h`).
