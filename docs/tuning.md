# Tuning values

These settings live in `esp_config.json` (and in profiles) and are edited in the ADVANCED tab (next patches). The ranges and defaults are written once, in `src/Tunables.cpp`; the settings file's checks use the same table, so a value outside its range is reported and clamped like any other (docs/config.md).
**The defaults are the values the smoothing was tuned with. Change one only when you have a reason, and compare with the readout.**

| Key | Default | Range (step) | What it does |
|---|---|---|---|
| `SmootherBaselineMs` | 12 ms | 2-50 (1) | How old the sample is that speed is measured against. Higher = steadier speed, slower to react to a change. |
| `SmootherBlendTauMs` | 10 ms | 0-50 (1) | How long the jump to each new sample is smeared out. 0 = the picture snaps at every sample; higher = softer, but lags a little. |
| `SmootherMaxAheadMs` | 50 ms | 0-100 (5) | How far past the newest sample the pose may be guessed. 0 = never guess; higher = smoother when the sample rate is low. |
| `SmootherTickMs` | 50 ms | 25-100 (5) | Length of one game tick, for interpolating other players. 50 ms = 20 TPS. Raise it only for a server that runs slow. |
| `AgentHz` | 120 | 10-240 (5) | Samples per second the game agent sends. `--hz N` on the command line overrides it for one run. |
| `ItemMinPx` | 6 px | 2-24 (1) | Smallest size an item is drawn at, so distant items stay visible. |
| `PlayerMinPx` | 2 px | 0-12 (1) | Smallest width/height of a player box; far players are drawn at least this big instead of vanishing. 0 = true size. |
| `MaxLabelDist` | -1 (unlimited) | -1, or a distance in meters | Names and distances are hidden beyond this. In the ADVANCED tab the top of the slider (510) is Unlimited. The GENERAL tab still cycles 25 / 50 / 100 m / Unlimited. |
| `LabelFadePct` | 30 % | 0-100 (5) | Names and distances fade out over the last N % of `MaxLabelDist` (at 100 m and 30 %: full up to 70 m, gone at 100 m). 0 = the old hard cut. Never fades when the label distance is Unlimited. |

`SmootherBaselineMs`, `SmootherBlendTauMs`, `SmootherMaxAheadMs` and `SmootherTickMs` are applied to the running overlay at once. `AgentHz` reaches the **running** game too: when the slider rests for 0.6 s the overlay runs the attach helper again with the new `hz`, and the agent (which supports being loaded a second time, `Agent.reload`) switches rate without restarting anything. The note under the slider says what happened. It retries a failed change after 5 s, at most three times, and then says so; moving the slider again starts over. With auto-inject off (`--no-inject`, you start the game with `-javaagent` yourself) nothing is attached for you: the note says to restart the game with `hz=N` (docs/launchers.md) or use `--hz N`. `--hz N` on the command line wins over the saved value for that run.

Files written before these keys existed simply get the defaults; no migration is involved.

## Far players (`PlayerMinPx`)

Player boxes used to be dropped when they came out under 2 px wide **or** tall. A player is 0.6 wide and 1.8 tall, so the width reaches 2 px at a third of the distance where the height does: at 1080p / 70 deg FOV a player vanished at about 232 blocks while the box was still 6 px tall (4K: about twice as far). Items already avoided this with a minimum marker size; players now do the same: a box smaller than `PlayerMinPx` (default 2 px) is grown around its centre instead of dropped. Whatever was drawn before is drawn identically; only players that used to disappear are new.
How far players are shown is decided by "Max player dist" (GENERAL tab), not by their size. In practice a server only sends players within its own tracking range (often 64-128 blocks), so this mostly matters on a single-player world or a server with a long view distance.

## Labels: fade and declutter (`LabelFadePct`, `LabelDeclutter`)

Names and distances used to be drawn the instant a player came inside `MaxLabelDist` and to vanish the instant they left it, and two players close together on the screen drew their names on top of each other.

* **Fade.** Inside the first `100 - LabelFadePct` % of the label distance a label is fully opaque; over the rest it fades in a straight line to nothing at `MaxLabelDist`. With the default 30 % and a 100 m label distance: opaque to 70 m, half as strong at 85 m, gone at 100 m. `LabelFadePct = 0` gives the old hard cut. An Unlimited label distance never fades. The distance text fades with the name, and so do item labels and the friend avatar.
* **Declutter** (GENERAL tab, "Label declutter"; default on). Once every box is drawn, labels are placed **nearest first**. A name that would overlap one already placed moves up by one line, up to three lines; a distance text moves down, up to two lines. A label that still finds no room is left out (the box stays) instead of being drawn unreadable. A dropped label reappears as soon as the crowd thins out, since the placement is redone every frame. Item labels are placed after the players', as one block each, and give way to them. Off = the old behaviour: far labels first, near ones drawn over them.

Nothing is allocated per frame for this; the placer holds up to 512 rectangles (256 players x name and distance), and labels beyond that are dropped, nearest kept.
