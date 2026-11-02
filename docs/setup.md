# First start and `mcesp --setup`

The first time you start `mcesp` (no settings yet) from a console, it checks this PC once and tells you in plain words what is missing:

    [ OK ] Settings folder C:\Users\you\AppData\Roaming\external-esp
    [ OK ] Agent           embedded in this exe
    [ OK ] UDP port        47615 is free
    [WARN] Java            no JDK found (JAVA_HOME / PATH)
           The attach helper first tries the game's own Java, which often has no Attach API. If attaching fails later, install a JDK 17+ ...
    [ OK ] Minecraft       javaw.exe (pid 4242)

| Item | OK when | If not |
|---|---|---|
| Settings folder | `%APPDATA%\external-esp` can be written | **FAIL**: nothing can be saved - check permissions / disk space |
| Agent | the jar (embedded, `--jar`, or next to the exe) exists | **FAIL**: build `agent\build.bat`, rebuild the exe, or `--jar` |
| UDP port | nothing else uses it (a real bind is tried) | **FAIL** with the reason and `--port` (also names Windows' reserved port ranges) |
| Java | a JDK is found (`--java`, `JAVA_HOME`, `PATH`) with Java 8+ and the `jdk.attach` module (read from its `release` file) | **WARN** if there is none or it is a runtime without Attach (the game's own Java is tried first when attaching, so this is not always a problem); **FAIL** only for a Java older than 8 |
| Minecraft | exactly one client is running | **WARN** when none (start it any time) or several (`--pid` / `--title`) |

After a **FAIL** it offers to copy a redacted diagnostics report to the clipboard. When one Minecraft is running and nothing failed it offers to run the self-test (docs/selftest.md: about 25 s in a loaded world). The first-run check repeats at every start until it has passed once, then never again on its own (`.setup-done` in the settings folder; delete it to see it again). Run it whenever you like with `mcesp --setup` (exit code 0, or 1 on a FAIL). Without a console (a pipe, a launcher that has none) it only prints; it never asks.
