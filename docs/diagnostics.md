# Diagnostics report

`mcesp --diagnostics [FILE]` prints (or writes to FILE) a text report you can paste into a bug report, then exits; the config window (Insert, GENERAL tab, SUPPORT) has the same text behind a **Copy diagnostics** button: it puts the redacted report on the clipboard (UTF-16, CRLF line ends)
and says "Copied N KB". The in-app report also knows the live state: the HUD status line, stream counters, renderer, and the names in the current snapshot. Code: `src/Diagnostics.*` (redaction and layout, no Windows dependency), `src/DiagnosticsApp.*` (gathering), `src/SysInfo.*`.

## What is in it
generated time, build id (`git describe` at CMake configure time, `unknown` for a hand-built exe), compiler, Windows version, logical CPU count, renderer (GPU/legacy), the
settings that matter for the stream (port, agent rate, token check on/off), the HUD status line, stream counters, and the **last 150 lines** of `esp_log.txt` (plus the end of
`esp_log.1.txt` if the current log is short) and `esp_agent.log`. Total size is capped at 256 KiB. It never contains `esp_config.json`, `friends.json`, the profile files or any recording.

## What is redacted (and what that means)
| Found in the text | Becomes |
|---|---|
| the local player, other players in the current snapshot, your friends list, names the logs announce (`Local username: X`, `user=X`) | `<local>`, `<player1>`, `<player2>` ... (the same name gets the same label throughout one report) |
| the Windows user name and computer name | `<user>`, `<host>` |
| the user profile, the exe folder, the agent's export folder | `%USERPROFILE%`, `<exe dir>`, `<export dir>` |
| any other `C:\Users\<name>` or `/home/<name>` | `C:\Users\<user>` |
| UUIDs | `<uuid>` |
| the session token and any `token=...` value | `<token>` |

Names are matched as whole words, ignoring case; a name that is part of a longer word is left alone. **Limits - read the report before you post it:**
* only names the program knows or the log lines above reveal are removed. A player's name that appears in a log line in some other form (for example in chat, if a future
  version ever logs it) is not recognised;
* names shorter than 3 characters are not redacted (they would also match ordinary words); a player named `Item` or `Mod` is redacted, and so are those words;
* server addresses, world coordinates and screenshots are not part of the report, but you may have pasted them elsewhere;
* it is a filter, not a guarantee.
