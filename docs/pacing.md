# Pacing: how evenly does the agent tick, and what can be done about it

The agent samples the game `--hz` times a second (default 120) from a daemon thread inside the JVM. Boxes are smoothed by the overlay from the
**stamped** sample instants, so uneven spacing is mostly harmless; what hurts is a tick that is late by several periods. This page says what the agent does
today, what the JDK sources say about it, how to measure it on your machine, and what to do with the result. Nothing here was measured on Windows by the
author of the code - that is exactly what the measurement tools are for.

## What the agent does today
`Thread.sleep(whole ms)` towards an **absolute** schedule (`next += period`): a late wake-up is made up by a shorter next sleep, and after a stall of more
than four periods it resynchronises instead of bursting. Option `pacer=hybrid` (`mcesp --agent-pacer hybrid`) sleeps until ~2 ms before the target and
spins on `System.nanoTime()` for the rest: precise, but it burns up to ~2 ms of a core per tick inside the game process. Default stays `sleep`.

## What the JDK sources say (read, not measured)
Read from the OpenJDK repository at tags `jdk-17+35`, `jdk-21+35` and `jdk-25+36`, `src/hotspot/os/windows/os_windows.cpp`:

| call | goes to | timer resolution |
|---|---|---|
| `Thread.sleep(ms)` | `PlatformEvent::park(Millis)` (JDK 21/25 via `park_nanos`, which rounds nanoseconds **up** to whole ms) | wrapped in `HighResolutionInterval`: if `ms % 10 != 0` it calls `timeBeginPeriod(1)` for the duration of the wait (17: lines 5279-5282, 5403; 21: 5192-5195, 5331; 25: 5406-5409, 5545) |
| `LockSupport.parkNanos(ns)` | `Parker::park` | **no** `HighResolutionInterval`: nanoseconds are floored to whole ms (minimum 1) and passed to `WaitForSingleObject` (17: 5499-5516; 21: 5427-5444; 25: 5651-5674), so it runs at whatever resolution the process already has |
| busy wait on `System.nanoTime()` | - | independent of the timer, costs CPU |

So the agent's `Thread.sleep(n)` for 1 <= n <= 9 ms (every tick at 120 or 240 Hz) asks for a 1 ms timer for the wait, and `parkNanos` would not. That is
why the agent uses `Thread.sleep` and why `PacingBench` includes `park` for comparison.

## What is NOT known (needs a Windows run)
* What a wake-up from `WaitForSingleObject` with a 1 ms `timeBeginPeriod` actually looks like on your Windows build and CPU power plan: expect "usually
  within 1-2 ms", but a number is what matters (p95 / p99 / max below).
* Whether Windows ignores `timeBeginPeriod` for a process in some states. From memory of Microsoft's documentation (**not checked here**): since Windows 10
  2004 the request is per process, and on Windows 11 a process without a visible window may not get better than the default resolution. The game's JVM has a
  visible window while you play; a minimised game is another matter.
* Whether the spin of `hybrid` costs frames in the game on your CPU. It is off by default for that reason.

## Measuring
1. **Bench in a fresh JVM** (no game needed; ~40 s):
   `java -cp esp-agent.jar mcradar.PacingBench 3` -> `esp_pacing_bench.txt`: for 60 / 120 / 240 Hz and `sleep`, `park`, `hybrid`, `spin`: mean, p50, p95, p99,
   max interval, share of late ticks (> 1.5 periods) and the CPU the thread used.
2. **In the game** (what really matters): `mcesp --pacing-report 20` (or the agent option `pacing=20`) -> after 20 s the agent writes `esp_pacing.txt` next to
   the jar: the same statistics for the real tick loop at the configured rate, plus the pacer in use. Run it while playing normally, once with the default and
   once with `--agent-pacer hybrid`.
3. **The overlay itself**: `mcesp --pacing-test [S]` -> `esp_pacing_overlay.txt`: the timer resolution Windows reports (`NtQueryTimerResolution`) before and after
   `timeBeginPeriod(1)`, how long `Sleep(1)` really takes with each, how a high-resolution waitable timer (Windows 10 1803+) compares, and the overlay's own frame
   pacing (`Sleep(1)` + yield-spin) at 60 / 120 / 240 Hz. The overlay's pacing is **not changed** by this; the file is what decides whether it should be.
4. Send the files (they contain no names or paths).

Seen so far: the three tools were run only on Linux and under Wine (the latter prints an emulated "Windows 10 build"); those numbers describe Linux and Wine, not Windows, and are
deliberately not quoted here.

## Reading the result
* `late(>1.5x)` under about 2% and `p99` under 1.5 periods: nothing to fix; leave the default.
* `late` clearly higher with `sleep` and the bench shows `hybrid` fixing it at an acceptable `thread CPU`: use `--agent-pacer hybrid`, and tell me - then it
  should become the default with the measured numbers as the reason.
* Both bad: the stalls are not timer granularity (GC pauses, the game's own load, power plan). More precise waiting will not help; look at `max` and the log.
