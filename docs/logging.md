# Logging

Two logs, one format:

| Log | Written by | Where |
|---|---|---|
| `esp_log.txt` (+ `esp_log.1.txt`) | the overlay (`src/Logger.*`), also mirrored to the console | next to the exe |
| `esp_agent.log` (+ `esp_agent.1.log`) | the agent inside the game (`agent/src/mcradar/Log.java`) | the agent's export folder (`--open-config` shows it) |

```
HH:MM:SS.mmm [LEVEL] [source] message         e.g.  14:03:22.517 [WARN ] [esp] Datagram with a WRONG session token dropped: ...
```
`LEVEL` is `ERROR`, `WARN ` , `INFO ` or `DEBUG` (padded to 5 characters); `source` is `esp` or `agent`. Merge the two files with `sort` and you get one timeline
(the date is in the session banner; times are local).

* **Levels.** The default is INFO. `mcesp --verbose` (or `-v`) switches both the overlay and, through the agent option `verbose=1`, the agent to DEBUG.
* **One message = one line.** Newlines, tabs and other control characters in a message become spaces, and a message is cut at 4000 characters, so nothing sent by the
  game or the network can forge a log line.
* **Size cap.** Each file is capped at 1 MiB. When it is full it is renamed to `*.1.*` (replacing the previous backup) and a new one is started, so a log never
  uses more than about 2 MiB however long the session runs. An oversized log left by a previous run is rotated at start-up.
* **Crash safety.** Every line is flushed when it is written.
* **Flood protection.** Warnings about the datagram stream (wrong token, unsupported version) are logged once per kind, not once per datagram.
