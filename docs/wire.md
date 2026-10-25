# Wire format (agent -> overlay)

The agent sends one UDP datagram (to `127.0.0.1:47615`) about 120 times a second. It is plain UTF-8 text, one record per line. The producer
is `agent/src/mcradar/Snapshot.java` (`encode`), the consumer is `src/Wire.cpp` (`ParseDatagram`). `tests/data/wire/*.dgram` are real encoder
outputs that the C++ tests parse (see `agent/test/README.md`).

```
MCRADAR 5                                                            header: protocol version (1-5 accepted)
K <token>                                                            v5: the session token (see below), second line
T <nanoTime>                                                         v4: instant the agent read the game state (agent clock, ns)
S <status text>                                                      status line shown in the HUD
C <x> <y> <z> <yaw> <pitch> <fov|-> <mode>                           v3+: the game camera; mode 1 first / 2 third-back / 3 third-front
L <name> <x> <y> <z> <yaw|-> <pitch|-> <health|-> <w|-> <h|-> <uuid|->    the local player
P <name> <same columns as L>                                         every other player
F 1                                                                  v4: an item list follows (absent = keep the previous list)
I <x> <y> <z> <w|-> <h|-> <count> <key> <ench|-> <tags|->            v3+: a dropped item; key/ench/tags are percent-encoded
```

`-` means "unknown". Columns are separated by spaces or tabs, lines by `\n` (a trailing `\r` is ignored), unknown tags and lines shorter
than two characters are skipped. Versions 1 (yaw only), 2 (+ pitch, health), 3 and 4 (+ `T`, `F`) are still parsed; v5 adds the `K` line.

## Version handshake

Both programs are built from the same source and should be rebuilt together, but a mismatch (old jar next to a new exe, or the other way round)
must not fail silently:

* **Overlay -> agent**: the overlay starts the agent with `wire=N` in its option string, N = the newest protocol it can read (`wire::kMaxVersion`).
  The agent writes `min(N, Snapshot.WIRE_VERSION)`. An overlay that passes no `wire=` (an older build, or a hand-written `-javaagent` line) counts as v4, so a
  newer agent stays readable by an older overlay. The agent logs the negotiation to `esp_agent.log`
  (`Wire protocol: overlay reads up to v4, agent writes up to v4 -> sending v4`), with a warning when the sides disagree.
* **Agent -> overlay**: the header says which version follows. `ParseDatagramEx` reports `Ok`, `NotOurs` (not `MCRADAR <n>`: some other program, or garbage) or
  `UnsupportedVersion` (n outside 1..`kMaxVersion`). When an agent of an unreadable version is heard and nothing readable has come in for 3 s, the HUD status line
  says *"agent speaks wire protocol v6, this overlay reads v1-v4: rebuild mcesp.exe and esp-agent.jar together"* (and the log warns once per version). A readable but
  old agent (v1-v3: no `T` stamps) is flagged too: *"agent speaks the old wire protocol v3 (this overlay: v4): rebuild esp-agent.jar ..."*.
* `mcesp --no-inject` users pass the same option by hand: `-javaagent:esp-agent.jar=port=47615;hz=120;wire=4`.

## Session token (v5)

Anything on the machine can send a UDP datagram to `127.0.0.1:47615`. To keep other programs (and an agent left over from an earlier overlay session) from feeding
the overlay fake players, the overlay generates **128 random bits at start-up** (`BCryptGenRandom`; if the OS generator fails it refuses to start), hands them to
the agent it attaches (`token=<32 hex>` in the agent options, together with `wire=5`) and **drops every datagram that does not carry them**: a `K <token>` line,
compared in constant time. Dropped datagrams are counted (`StreamIngest::AuthFailures`), one warning per (reason, version) is logged however the failures interleave, and
the HUD says *"agent sends no session token (wire v4): rebuild esp-agent.jar together with mcesp.exe, or run mcesp --no-auth"* or *"datagrams with a wrong session token
are being ignored"*. Replay is exempt (a recording was authenticated when it was made; the token is stored in it, see `docs/esprec.md`).

* `mcesp --token T` uses a fixed token (16-64 letters/digits) and `mcesp --no-auth` turns the check off (logged as a warning). With `--no-inject` the exact
  `-javaagent:esp-agent.jar=port=...;wire=5;token=...` line is printed at start-up.
* A v4 agent has no token, so a token-checking overlay refuses it - rebuild the jar (an older exe still works with this jar: without `wire=` the agent speaks v4).

**What it does not protect against.** The token is visible in the command line of the short-lived attach helper process and in the target JVM's agent options,
so any process running as the same user can read it. It stops accidents and other programs, not malware that already runs as you; nothing can, because that
malware could equally attach its own agent to the game.

## The parser treats the datagram as untrusted input

Any process on the machine can send a datagram to the port, and a bug in the agent can send nonsense, so `ParseDatagram` validates everything
(constants in `src/Wire.h`, namespace `wire`):

| What | Rule |
|---|---|
| Token (`K`) | only in v5; 1-64 letters/digits, otherwise the line is dropped (and the datagram has no token) |
| Header | exactly `MCRADAR <1-3 digits>` then end of line or a space. `MCRADAR 4junk` is not ours (and `MCRADAR 10` is no longer read as v1: it is an unsupported version). A version outside 1..4 is rejected as `UnsupportedVersion` |
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
