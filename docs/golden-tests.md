# Golden tests: how much do the boxes jitter on screen?

A golden test replays a recording (`tests/data/<name>.esprec`) through the real pipeline - `ParseDatagram` -> `StreamIngest` -> `Smoother`
-> `Camera` - on a **fake clock** (no sleeping, no display, same result on every machine) and measures how much projected points move
irregularly on screen. The numbers are compared with `tests/data/<name>.golden.json`.

Code: `tests/golden.h/.cpp` (pipeline + metric + comparison), `tests/golden_runner.cpp` (command line), `tests/golden_selftest.cpp`
(tests of the harness itself).

## The pipeline (mirrors `EspOverlay::RenderFrame`)

Virtual frames at 240 Hz: replay due datagrams -> if there is a newer snapshot, `Smoother::Push` it -> `PredictInto(now)` ->
camera from the agent's exact camera (else reconstructed from the local player) -> `Camera::Project`. The first second (`warmup_s`) is
not measured: the smoother's clock-offset estimate needs a moment. Not covered: drawing, the real frame limiter, the GPU/GDI paths,
and the live socket drain (a replay publishes every datagram).

## Probes and the metric

* `world_anchor` - a fixed point in the world 8 m in front of the first camera. Whatever it does on screen is what the *camera* does.
* `player:<name>` - the centre of the box of every other player.

For consecutive frames i-1, i, i+1: `dev_i = | p_i - (p_{i-1} + p_{i+1}) / 2 |` in pixels (half the second difference of the position).
Smooth motion gives ~0, a box that stutters or vibrates around a smoothly moving target gives a large value. No ground truth is needed,
so the same metric works on real recordings. Reported per probe: `jitter_rms_px`, `jitter_p95_px`, `jitter_max_px`, `max_step_px`
(largest frame-to-frame movement) and `frames` (how many frames were measured).

## Pass / fail

Not bit-exact. A metric fails only if it got **worse** than the golden value by more than `+10 % + 0.05 px` (`jitter_max`: `+0.2 px`,
`max_step`: `+1 px`), if the number of measured frames changed by more than 1 %, or if a probe appeared or disappeared. Improvements pass
and are printed as a note. Override with `golden_runner --rel R --abs PX`.

## Changing goldens

```
tests/regen-goldens.sh            # all recordings (or: tests/regen-goldens.sh walk)
git diff tests/data               # READ it
```
Regenerate when a change is *meant* to move the numbers. If a number went up, that is a regression to fix, not a new baseline. Never
regenerate to make CI green without understanding the diff. The format of `*.golden.json` is flat JSON (`"probe/<name>/<metric>": number`).

## Adding a recording

Drop `name.esprec` in `tests/data/` and run `tests/regen-goldens.sh name`; CMake registers a `golden_name` ctest from the file pair. Record a
real session with `mcesp --record`, or generate a synthetic one (see `tests/tools`). Recordings contain player names and coordinates: use
a test account and check before committing.
