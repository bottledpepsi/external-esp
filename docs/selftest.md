# Self-test

One command that says what works in a running Minecraft, in a form you can paste into a bug report.

## Agent side (no overlay needed)
```
java --add-modules jdk.attach -cp esp-agent.jar mcradar.SelfTest <pid> [seconds]
```
(`--add-modules jdk.attach` is needed on JDK 9+ only.) It attaches the agent - or, if the overlay already loaded it, tells the running agent to run the
self-test **without touching its port or token** - and watches for `seconds` (default 20): look around with the mouse and drop an item meanwhile. Then it prints
the report and exits with its code. The same report is written by the agent option `selftest=SECONDS` (`selftestout=FILE`, default `esp_selftest.txt`).
A self-test also turns on the structure dump (`esp_structure.json`, docs/fixtures.md).

## The report
```
esp-selftest 1
agent: build 88b0c442 (101 KB, modified 2026-10-26), wire 5       <- which agent jar is really in the game
java: ...   os: ...   minecraft: 1.21.1                           <- from version.json / --version; "unknown" if neither exists
CHECK camera PASS exact camera found (value-match)
CHECK fov    WARN no FOV method found: ...
RESULT WARN
EXIT 2
```
Checks: `game`, `world`, `local-player`, `players`, `camera`, `fov`, `items`, `pacing`, `send`. Each is `PASS`, `WARN` (works, worse than it should),
`FAIL` (should work and does not) or `SKIP` (the situation did not occur: nobody else on the server, the view never moved, no item lay around - **not** a
pass). No player names, tokens or machine paths are in it.

| exit | meaning |
|---|---|
| 0 | everything passed |
| 1 | passed, but something could not be exercised (see the SKIP lines) |
| 2 | warnings |
| 3 | failures |
| 10 | not a Minecraft JVM (the player-profile class is not loaded) |
| 11 | not in a world yet: join a single-player or server world and repeat |
| 12 | (overlay) no data arrived over UDP |
| 13 | (overlay) several Minecraft clients found: choose with `--pid` or `--title` |
| 14 | attach failed |
| 15 | the agent loaded but no report arrived in time |

Tested on Linux with fake games only (`agent_smoke.py selftest-*`): a pass, a JVM that is not Minecraft (10), and the realistic game. No real Minecraft was
available while writing it.
