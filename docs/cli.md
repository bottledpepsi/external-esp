# Command line

(`mcesp --help` prints the options; this page has what does not fit there.)

## Exit codes

| Code | Meaning |
|---|---|
| 0 | success |
| 1 | something failed while running (graphics could not start, an unexpected error) |
| 2 | wrong command line (unknown option, missing or bad value, unknown profile) |
| 10-16 | `--selftest` outcomes, see docs/selftest.md (16 = the UDP port is in use, also outside the self-test) |
| 17 | another mcesp is already running on this port |
| 18 | no usable agent jar |

The values live in `src/ExitCodes.h`; a test checks they do not collide with the self-test's.

## One overlay per port

Starting a second `mcesp` on the same UDP port prints who has it and exits with 17:

    mcesp is already running (process 1234) on port 47615.
      - Close it first (hold the exit hotkey, or close its console), or
      - to run a second client on this PC, start this one with  --port <another port> --pid <the game's process id>.

Two clients on one PC use two ports and are independent. `--demo` and `--replay` take no port and are not checked.

## Checking the command line

The options are listed once, in `src/CliSpec.cpp`; `--help` is generated from that table, and the whole command line is checked before anything starts:

    > mcesp --prot 5
    Unknown option '--prot'. Did you mean '--port'?
    Try:  mcesp --help
    > mcesp --hz 500
    --hz: '500' is not a whole number from 1 to 240.
    > mcesp --port
    --port needs a value (N).

All of these exit with 2. (`--hz` used to clamp a too large number silently and `--port abc` became port 0; both are errors now.) A test compares the table with the options `main.cpp` handles, so a new option cannot be added to one and forgotten in the other.

## Start-up messages and what to do

| You see | Meaning / what to do | Exit |
|---|---|---|
| `UDP port N is already in use by another program` | something else owns the port (a stuck mcesp, another tool). Close it, or `--port <other>` - the agent is told the same port automatically. | 16 |
| `Windows does not allow UDP port N here ...` | the port is inside a range Windows reserved (Hyper-V / WSL / Docker). `netsh int ipv4 show excludedportrange protocol=udp` lists them; pick one outside. | 16 |
| `There is no agent jar: ...` | the exe was built without `agent\esp-agent.jar` and none lies next to it. Run `agent\build.bat`, rebuild, or `--jar PATH`. | 18 |
| `mcesp is already running ...` | see above | 17 |
| `no java.exe found (install a JDK 17+ or pass --java <path>)` (HUD / log) | the attach helper needs a JDK; the game's own JRE often has no Attach API. `--java "C:\...\jdk-21\bin\java.exe"`. | - |
| `waiting for minecraft` (HUD) | no Minecraft client was found yet; with several, `--pid N` or `--title`. | - |

The last two stay on the screen while the overlay waits (the game may start later), so they have no exit code.

## Leaving

The overlay ends when you hold the exit hotkey (End, 3 s), close its window, press **Ctrl+C** in its console, close the console, or log off / shut down Windows. All of them run the same fixed sequence (`src/Shutdown.h`), each step timed and guarded, so one that fails or is slow cannot stop the rest; the log line `Shutdown (Ctrl+C): hotkeys 0 ms, settings 4 ms, agent 912 ms, ...` shows how long each took:

1. **hotkeys** - the system hotkeys go back to Windows first;
2. **settings** - unsaved changes are written;
3. **agent** - the agent inside the game is told to pause (`paused=1`, through the same reload as a rate change) so it stops sampling at 120 Hz for nobody; waits at most 3 s. Skipped if the agent never sent data (attaching just to pause it would load it), with `--no-inject`, in `--demo`/`--replay`. If it times out the agent keeps running until the game closes or an overlay starts again (which resumes it);
4. **producers** (the UDP listener, replay driver), then **recorder** (drains and closes the `.esprec` only after nothing writes to it), skin downloads, the timer resolution.

When the console is *closed*, Windows ends the process after about 5 s; the handler waits up to 4.5 s for the sequence to finish. A second Ctrl+C while it is shutting down ends the process at once (exit code 130). If the overlay itself crashes or is killed, nothing runs and the agent stays active in the game until it closes; starting the overlay again takes it over.
