# Launchers, clients and how the game is found

**Status: nothing here was tested on a real launcher.** What is described is what the code does and what to try; the "untested" rows are expectations,
not results. If one works or fails for you, the self-test report (docs/selftest.md) is the thing to send.

## How the client is chosen
`mcesp` scans the java processes (`java.exe`, `javaw.exe`) and picks the one whose **window title starts with "Minecraft"** - the game sets that title itself,
so vanilla, Fabric, Forge, NeoForge and instances started by Prism, MultiMC or ATLauncher should all match. `--list` shows what it sees.

| situation | what happens |
|---|---|
| exactly one window titled "Minecraft..." | that process |
| two or more (two accounts for a LAN test, two instances) | **none is chosen**; the HUD and the log list them: `2 Minecraft clients found: PID 100 'Minecraft* 1.21.1', PID 101 '...' - choose one with --pid <PID> or --title <text>` |
| no such window, but other Java windows exist (an IDE, a custom client) | none is chosen; they are listed with the flag that picks one |
| java processes without a window (Gradle daemon, IDE background JVM, a server) | never chosen unless `--pid` names them |

Choosing explicitly: `--pid N` (exact), `--title TEXT` (window title contains TEXT, case-insensitive), `--exe TEXT` (program name contains TEXT, e.g. `javaw`);
`--title` and `--exe` combine. A custom client whose window is not titled "Minecraft" (Lunar, Badlion, Feather, ... - **untested**) needs `--title lunar` or `--pid`.

*Behaviour change in this release:* earlier versions took the first java process when no window was titled "Minecraft", and the first of several clients. Both
are now refused with the message above, because the agent would otherwise be loaded into whatever happened to be first - an IDE, say.

## Attaching
The agent is loaded with the Attach API through a `java` that has the `jdk.attach` module. Tried in this order: the game's own `java.exe`, `--java`,
`JAVA_HOME`, `PATH`. Mojang's bundled runtimes and some launcher-supplied runtimes may lack that module (not checked); then a JDK on the machine is used, or pass
`--java "<JDK 17+>\bin\java.exe"`. On failure the HUD and `esp_log.txt` give a hint, for example:

* *access denied / OpenProcess*: mcesp and the game must run at the same privilege level (both normal, or both as administrator);
* *no Attach API*: install a JDK 17+ and use `--java`;
* *AttachNotSupportedException*: the JVM refused. A JVM flag (`-XX:+DisableAttachMechanism`), another user account or a launcher wrapper can cause it.

**Manual route (always works if the game's JVM arguments can be edited):** run `mcesp --no-inject`; it prints a line like
`-javaagent:esp-agent.jar=port=47615;hz=120;wire=5;token=...`. Add it to the instance's JVM arguments (Prism/MultiMC: Edit instance -> Settings -> Java ->
JVM arguments; vanilla launcher: Installations -> More options -> JVM arguments), with the full path of `esp-agent.jar`, then start the game.

*Java 21 and later:* loading an agent after start prints a warning on the game's stderr, and the JDK documentation (JEP 451, from memory - not re-checked
here) says a future release will refuse it unless the game starts with `-XX:+EnableDynamicAgentLoading`. If attaching stops working after a Java upgrade,
add that flag, or use the manual route (an agent given with `-javaagent` at start is not affected).

## Restarting
Starting `mcesp` while the game already runs a previous session's agent works: the agent takes the new port and session token over (a second load
reconfigures it). The game can be closed and restarted while `mcesp` runs; it waits for the next client.

## Not covered
Anything that stops other software from loading an agent into the game, or from drawing over it, is respected, not worked around: this project does not hide
itself from, or evade, anything. Servers whose rules forbid overlays are the user's responsibility.
