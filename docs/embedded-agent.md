# The agent jar inside the exe

`mcesp.exe` carries `esp-agent.jar` as a resource (`ESPAGENT`, `RCDATA`), put there by CMake or `build-all.bat` from `agent\esp-agent.jar` - **build the agent first** (`agent\build.bat`), or the exe is built without it (CMake warns) and falls back to a jar next to the exe.

## What happens at start

1. `--jar PATH` wins if you give it.
2. Otherwise the embedded jar is written to `%LOCALAPPDATA%\external-esp\agent\esp-agent-<12 hex of its SHA-256>.jar` and handed to the attach helper from there. The log says `Agent jar: embedded in this exe (sha256 ...) -> ...` and whether it was extracted now, reused, or rewritten.
3. Only if there is no usable embedded jar: `esp-agent.jar` next to the exe.

An embedded jar wins over an `esp-agent.jar` left next to a *newer* exe on purpose: the two belong together (a stale jar speaks an older wire version).

## Safety

- The name contains the hash, so an update never overwrites the jar a still-running game has open; a new exe simply writes a new file. Older `esp-agent-*.jar` files are removed on start when possible (a running game keeps its own, which is fine; it is retried next time).
- An existing file is used only if its hash equals the embedded jar's. A truncated or modified copy is rewritten (`...does not match its hash - writing it again` in the log). A write goes to a temporary file, is flushed and renamed, so there is never a half-written jar under the final name.
- The agent's own log (`esp_agent.log`) and the self-test report still go **next to the exe**, not into the cache folder.

## One file

The exe also needs no `libwinpthread-1.dll` when the toolchain can link it statically (`ESP_STATIC_WINPTHREAD`, default on; tested at configure time, and the same fallback is in `build-all.bat`). If it cannot, the DLL is copied next to the exe as before and the build says so. Windows also needs the usual system DLLs (Direct2D, DirectWrite ...), nothing else.
Manual `-javaagent` users (`--no-inject`): the start-up line prints the full path of the extracted jar to use.
