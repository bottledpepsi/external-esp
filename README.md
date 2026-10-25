# external-esp (C++ port)

External Direct2D overlay for a **local Minecraft Java world** (single-player / LAN with two accounts on your own PC).
A version-independent Java agent is loaded into the game's JVM; it streams players, dropped items (with enchantments/tags),
and the **game's own camera and FOV** over loopback UDP. The C++ app draws boxes / skeletons / tracers / names / health /
items / minimap in a transparent click-through window, with the original tool's 7-tab config window, friends (with real skin
faces) and profiles.

> Scope: written for reverse-engineering practice on worlds you control. Do not use it on servers you do not own.

```
Minecraft JVM ──(JVM Attach API)── esp-agent.jar ──UDP 127.0.0.1:47615──▶ UdpListener thread
 (finds players/items/camera by structure,                                  │ immutable snapshot, pointer swap
  exports esp_layout.json)                                                  ▼
            EspOverlay (UI thread): Smoother → Camera → EspRenderer + ConfigUi → Gfx (D2D) → UpdateLayeredWindow
                                              SkinCache thread: WinHTTP + WIC  → friend avatars
```

## Build (MinGW-w64, C++20)
Requirements: **JDK 8+** (builds the agent; also what runs the attach step) and **MinGW-w64 g++ 11+ with POSIX threads** (CLion's bundled one, MSYS2).

| Way | Command |
|---|---|
| One step | `build-all.bat` → `dist\mcesp.exe` + `dist\esp-agent.jar` (+ `libwinpthread-1.dll`); finds CLion's/MSYS2's g++ itself |
| CLion / CMake | open the folder; first run `agent\build.bat`; build target `mcesp` (jar + DLL are copied next to the exe) |
| Manual | `agent\build.bat`, then `g++ -std=c++20 -O2 src/*.cpp -o mcesp.exe -ld2d1 -ldwrite -ldwmapi -lwinmm -lgdi32 -luser32 -lws2_32 -lwinhttp -lwindowscodecs -lole32 -luuid -lbcrypt -static-libgcc -static-libstdc++`, then put `esp-agent.jar` and `libwinpthread-1.dll` next to it |

Linker libs: `d2d1 dwrite` (Direct2D/DirectWrite) · `dwmapi` (DwmFlush = VSync) · `winmm` (1 ms timer) · `gdi32 user32` · `ws2_32` (UDP) ·
`winhttp` (skin downloads) · `windowscodecs ole32 uuid` (PNG decoding via WIC) · `bcrypt` (the per-session token). **Do not pass `-mwindows`** - the console subsystem is intentional (logs also go to `esp_log.txt`).
Do not use `-static`: it fails on CLion 2026's bundled GCC 15 (`undefined reference to __intrinsic_setjmpex`); the exe needs `libwinpthread-1.dll` next to it (copied automatically).

## Continuous integration
`.github/workflows/ci.yml` runs on every push and pull request: the core tests on Linux, the agent on JDK 17/21/25 (build, Java tests, smoke test against the
fake games) and a Windows MinGW-w64 build with the native tests. The Windows job prints its `g++` version in the step summary (it uses MSYS2's current
UCRT64 GCC, which may differ from CLion's bundled GCC 15.2) and uploads `mcesp.exe`, `esp-agent.jar` and `libwinpthread-1.dll` as the `mcesp-windows-x64` artifact.

### Sanitizers
`cmake -S tests -B build/san -DESP_SANITIZE=address,undefined && cmake --build build/san -j && ctest --test-dir build/san --output-on-failure` runs the core tests under
ASan + UBSan (including `float-cast-overflow`, which `-fsanitize=undefined` does not enable); `-DESP_SANITIZE=thread` runs them under TSan. CI does both.

## Run
```
mcesp                      # normal: finds Minecraft, attaches the agent, draws the ESP
mcesp --demo               # synthetic players, no Minecraft needed (try the UI / friends / profiles)
mcesp --list               # list java processes (two clients on one PC?)  then:  mcesp --pid 12345
mcesp --diagnostics r.txt    # write a REDACTED report (versions, settings, log tails) to attach to a bug report (docs/diagnostics.md)
mcesp --verbose              # also log DEBUG lines (esp_log.txt is capped at 1 MiB, one backup esp_log.1.txt)
mcesp --record s.esprec     # also save the raw agent datagrams + arrival times (docs/esprec.md) for replay / tests / bug reports
mcesp --replay s.esprec    # play a recording back (no Minecraft needed): F6 pause, F7 step, F8/F9 speed; --replay-speed X, --replay-loop
mcesp --screen             # overlay the whole monitor instead of tracking the game window
mcesp --no-inject          # don't auto-attach; prints the exact -javaagent line (with this session's token) to start the game with; --token T fixes the token, --no-auth turns the check off
mcesp --java "C:\...\jdk-21\bin\java.exe" --open-config --port 47616 --jar C:\path\esp-agent.jar
```
Hotkeys (rebindable): **Insert** config · **Delete** toggle ESP · **hold End 3 s** exit. They act only while the game or overlay has focus.
Opening the config removes click-through and takes keyboard focus; closing it (or clicking back into the game) restores both.

## The three data sources
**Items (agent).** Item entities are found without any Minecraft name. An *ItemStack* is a small class with an `int` (the count; `0` = empty stack) and a resolvable item id: a public no-arg `String` method returning a translation key
(`item.minecraft.diamond_sword`, legacy `item.diamondSword`), or - if the stack has no such method - the id from its item reference (an Item object with that method, or a registry holder printing `ResourceKey[minecraft:item / minecraft:diamond_sword]`).
An *item entity* is a positioned entity from which a non-empty stack is **reachable through a short path of small objects**: in real Minecraft the `ItemEntity` has no stack field, the stack sits in `entityData -> DataItem[] -> value`
(next to `initialValue`, which holds the empty stack), so the path is found by a bounded graph search per entity class and replayed per entity. A ~0.25-block hitbox filters item frames/arrows.
They are read from the level's entity storage (found by generic element type / runtime elements). Per item the agent sends position, hitbox, count, id, enchantments and the tag text:
up to 1.20.4 the stack's NBT prints itself as SNBT (`{Enchantments:[{id:"minecraft:sharpness",lvl:5s}]}`) and enchantments are parsed from it; from 1.20.5 the component graph is searched for
`Map<Holder,Integer>` enchantment maps. The nearest 48 items are sent; labels show `Name xN`, the enchantments (curses red) and optionally the raw tags (ITEMS tab).

**Camera (agent).** The camera object is identified **by value**: its position stays near the local player's eye and two of its float fields track the player's yaw and pitch (this needs the player to
turn their head a little; until then the HUD says `cam: reconstructed`). The FOV comes from the game itself: the renderer's method taking `(Camera, float, boolean)` or `(float, boolean)` and returning degrees -
the one the game uses to build its projection - so sprint/speed/bow/underwater effects are included. Camera distance from the eye gives **first / third person (behind / in front)**.
The C++ side uses that exact pose and FOV; if the camera or FOV method cannot be found it falls back to the player's eye + the configured FOV. Entity **hitbox sizes** (sneaking = shorter) come from the AABB field.

**Skins (C++).** Friends get their real skin face (same pipeline as the C# tool): UUID from the agent if it is an online-mode (v4) UUID, else Mojang API by name → disk cache → Crafatar 16x16 avatar → fallback:
session server textures → skin PNG → head + hat layer composited. WinHTTP downloads, WIC decodes, results are cached in `%TEMP%\mc_esp_avatars`; failures retry after 60 s; a letter placeholder shows meanwhile.
Base URLs can be overridden with `ESP_MOJANG_API`, `ESP_CRAFATAR`, `ESP_SESSION_API` (mirrors / tests).

## Wire format v3 (`Wire.h`, `agent/.../Snapshot.java`)
`MCRADAR 3 / S status / C x y z yaw pitch fov mode / L|P name x y z yaw pitch health w h uuid / I x y z w h count key ench tags` (free text percent-encoded, `-` = unknown). v1 and v2 packets still parse.

## Config & persistence
`esp_config.json`, `friends.json`, `profiles/<name>.json` next to the exe; files from the C# tool load unchanged. Added keys: `Fov` (fallback), `VSync`, `Smoothing`, `ShowHealth`, `ShowItemEnchants`, `ShowItemTags`.
Tabs: GENERAL · PLAYERS (+friends) · TRACERS · ITEMS · MINIMAP · PROFILES · HOTKEYS. Left-click cycles a preset, right-click goes back, the title bar drags, the wheel scrolls.

## Tests performed (and what was NOT)
* Native unit tests (`tests/run-tests.sh`, needs cmake; it builds `tests/CMakeLists.txt` and runs ctest): config/JSON (incl. C# files), friends, profiles, wire v1-v3 (camera, hitbox, uuid, items with percent-decoded tags), item text, projection at 4 yaws/pitch/FOV edge, smoother (players + camera). All pass.
* Agent vs a fake game with Minecraft-like structure (obfuscated names; Vec3 and plain-double layouts; decoys): players/local/yaw/pitch/health/hitbox/uuid; **camera + private `getFov` method found by reflection with FOV sweeping 63-77**; first- and third-person (distance 4.00, mode 2);
  items in NBT style (both `id`/`lvl` orders) and component style, item frame filtered; JDK 17 and 21.
* Full chain under **Wine + Xvfb**: agent → UDP → overlay with `cam: exact`, items with enchantment lines and tag text; config UI driven with synthetic input; skins against a local mock of Mojang/Crafatar/session servers
  (Crafatar path and the session-texture fallback incl. hat compositing).
* **Not tested:** a real Minecraft install, real-Windows behavior (click-through/focus, `DwmFlush` pacing, Attach on Windows), the live Mojang/Crafatar services, and discovery against real Minecraft classes. Treat the first run as a test; read `esp_log.txt`, `esp_agent.log`, `esp_layout.json`.

## Known limits
* **Camera/FOV discovery is heuristic.** It needs a few seconds of head movement; if the renderer's FOV method has a different signature in your version, the pose is still exact but FOV falls back to the config value (HUD/GENERAL tab say which). Zoom/FOV mods may differ.
  Calibrate in first person; in front-view third person the angle match accounts for the 180° flip.
* **Enchantment/tag extraction** relies on `toString()` of NBT tags (<=1.20.4) and of component holders (>=1.20.5); legacy numeric enchantment ids show as `Enchant #16`. Items that are not dropped items but carry a stack (fireworks, thrown potions) can appear as items.
* **Skins:** need internet; offline/cracked players have v3 UUIDs, so they are looked up by *name* (like the original) - a premium account with the same name shows that account's skin, unknown names keep the letter placeholder.
* Health shows only where the method is literally `getHealth`. Exclusive fullscreen can't be overlaid. Two clients on one PC: pass `--pid`.
* Newer JDKs restrict `sun.misc.Unsafe` (add `--sun-misc-unsafe-memory-access=allow`) and warn about dynamic agent loading (`-XX:+EnableDynamicAgentLoading`, or `--no-inject` + `-javaagent`).

## Troubleshooting
| HUD / log says | Fix |
|---|---|
| `waiting for minecraft` | game not running / window not titled "Minecraft": try `--list`, `--pid`, `--screen` |
| `attach failed ... no Attach API` | install a JDK 17+ and pass `--java` |
| `cam: reconstructed` | normal until you turn your head; if it never changes see `esp_layout.json` → `camera` |
| `SkinCache: GET ... WinHTTP error 12029/12007` | no network / blocked; set `ESP_*` mirrors or accept placeholders |
| `Cannot listen on 127.0.0.1:47615` | another instance is running; use `--port` |
| `agent jar not found` | run `agent\build.bat`; keep `esp-agent.jar` next to `mcesp.exe` |
| `agent ok: ... 0 item(s) [items A/B ent]` | `B` = entities the agent can see, `A` = how many were recognised as item entities. `B = 0`: entity container not found (see `esp_agent.log`, "Entity container"). `B > 0, A = 0`: open `esp_items_debug.txt` (written ~5-35 s after the problem starts, next to `esp_layout.json`) - it lists each entity class, what is reachable from it and where the search stopped. Send that file + `esp_agent.log`. |

## Layout
`src/` C++ (Logger, Json, Presets, EspConfig, FriendList, ProfileManager, Wire, ItemText, Camera, Smoother, ProcessManager, AgentInjector, AgentSupervisor, UdpListener, SkinCache, Gfx, EspRenderer, ConfigUi, EspOverlay, main) ·
`agent/src/mcradar/` Java (Agent, Attach, Probe, AuxSearch, CameraScanner, ItemScanner, Tags, Mem/Acc/FieldPath, Snapshot) + `build.bat|sh` + `test/` fake game · `tests/` host unit tests.
