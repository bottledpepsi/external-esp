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

`SmootherBaselineMs`, `SmootherBlendTauMs`, `SmootherMaxAheadMs` and `SmootherTickMs` are applied to the running overlay at once. `AgentHz` needs the agent to be told (see docs/launchers.md for the manual `-javaagent` option).

Files written before these keys existed simply get the defaults; no migration is involved.
