# Windows manual checklist

Things that cannot be tested without a real Windows machine and a real GPU. Each item says what to do, what you should see, and what to send if you do not.
Nothing in this list has been run by the author of the code (it was written without Windows hardware); the unit tests cover only the decision logic
named next to each item. Please send the `esp_log.txt` (and `mcesp --diagnostics`) with any item that does not behave as described.

## 1. GPU device loss and recovery
Logic: `src/GpuRecovery.h` (tests/gpu_recovery_test.cpp, 68 checks with injected failures and fake time).

| step | expected |
|---|---|
| start `mcesp --simulate-device-loss`, wait for the overlay, press **F10** | log: `--simulate-device-loss: injecting ...`, `GPU device lost in Present (simulated): 0x887A0005 DXGI_ERROR_DEVICE_REMOVED ...`, `GPU device lost - recreating the renderer`, `GPU renderer recovered in N ms`. The overlay blinks at most once and keeps drawing. |
| press F10 five more times, 2-3 s apart | recovers each time; no "switching to the legacy" line (a loss every few seconds is not a reason to give up unless 4 happen within 30 s) |
| press F10 four times within 30 s | `GPU device could not be recovered ... switching to the legacy GDI renderer`, `Renderer: legacy ... after a GPU device loss`; the overlay window is replaced and keeps drawing |
| start with `--simulate-device-loss=persistent`, press F10 | every recovery fails (`InitGpu: failing on purpose`), retried after 0.25, 0.5, 1, 2, 4 s (about 8 s), then the switch to the legacy renderer; CPU use stays low while waiting (the frame loop sleeps) |
| the real thing: **Win+Ctrl+Shift+B** (Windows restarts the graphics driver) | the screen flickers; log shows a real `DXGI_ERROR_DEVICE_REMOVED/RESET` from Present or EndDraw and a recovery; the overlay returns |

The simulated loss injects the error code at the next `Present`; the recreation that follows (device, swap chain, Direct2D, DirectComposition) is the real code.

## 2. DPI and several monitors
Code: `src/DpiInfo.cpp` (per-monitor v2 awareness through `SetProcessDpiAwarenessContext`, falling back to per-monitor, then system aware), `WM_DPICHANGED` /
`WM_DISPLAYCHANGE` handling in `EspOverlay`. Pure text helpers: tests/dpi_format_test.cpp. Under Wine (which only offers system awareness) the fallback line and the
monitor list were seen in the log; nothing about real scaling was.

Why it matters: a system-aware process gets coordinates *virtualised* for every monitor whose scale differs from the primary's. The overlay is placed from the game
window's rectangle in screen pixels, so over a game on a second monitor with another scale it could be offset or the wrong size.

| step | expected |
|---|---|
| start `mcesp`, read `esp_log.txt` | `DPI awareness: per-monitor v2 (SetProcessDpiAwarenessContext)` (Windows 10 1703+) and `Monitors: monitor 1: 2560x1440 at (0,0), 150% (144 dpi), primary; monitor 2: ...` with the right scales |
| game on the primary monitor, 100% / 125% / 150% / 200% scale | boxes sit on the players; the overlay covers exactly the game's client area |
| game on a second monitor with a *different* scale, windowed | same - no offset, no size error. This is the case the old `SetProcessDPIAware()` could get wrong |
| drag the game window from one monitor to the other | the overlay follows; `WM_DPICHANGED` is logged, and the window is re-placed from the game's rectangle (the size Windows suggests for the new scale is deliberately ignored) |
| change the display resolution or plug a monitor in/out while running | `WM_DISPLAYCHANGE: monitor ...` in the log; the overlay re-fits within a frame or two |
| `Game window DPI awareness:` in the log | informational: "unaware" games (very old launchers) are stretched by Windows; the overlay still uses physical pixels, which is what the stretched window occupies |

## 3. Window states, fullscreen and the overlay staying on top
Logic: `src/WindowState.h` (tests/window_state_test.cpp, 21 checks). Facts from Win32: `src/WindowTracker.cpp`. A probe that creates stand-in game windows runs under Wine
(9 checks passed there) and in CI as an informational step (`tests/windows/wintrack_probe.cpp`); neither is Windows with a real GPU and a real game.

What the overlay does: every 250 ms it classifies the game window (windowed / borderless fullscreen / **exclusive fullscreen suspected** / minimised / hidden or on
another virtual desktop), logs a change, and - while shown - checks that it is still above the game in the z-order. A topmost game window (GLFW's fullscreen) that was
re-activated lands above our topmost overlay, which then looks "blank"; the overlay re-asserts TOPMOST (at most every 250 ms) and logs the first time.

| step | expected |
|---|---|
| windowed game; resize, move, maximise | log: `Game window is now: windowed ...` once; the overlay follows every frame |
| minimise the game, restore it | overlay disappears and comes back; `minimised` / `windowed` in the log |
| Minecraft F11 fullscreen | `borderless fullscreen`; the overlay is visible. If it is not: `exclusive fullscreen (suspected)` and a warning telling you to switch to windowed/borderless - please send the log lines |
| alt-tab away and back (fullscreen) | the overlay is above the game again within about a quarter of a second; log: `Overlay was below the game window in the z-order; raised it` |
| move the game to another virtual desktop (Win+Tab) | `hidden`; the overlay is not shown |
| a second monitor: fullscreen on each | `monitor WxH` in the log is the one the game is on |

## 4. Which GPU, hybrid graphics, no GPU
Logic: `src/AdapterChoice.h` (tests/adapter_choice_test.cpp, 17 checks on hand-made laptops, a VM and bad flags). `InitGpu` lists every DXGI adapter, chooses, then walks:
chosen adapter -> Direct3D's default hardware adapter -> **WARP** (the software rasteriser) -> (the existing fallback) the legacy GDI renderer. Under Wine the listing, the
decision text, `--adapter 0/9/nvidia/radeon` and the explicit-adapter device creation ran against Wine's single emulated adapter; nothing about a real multi-GPU machine did.

| step | expected |
|---|---|
| any machine: read the start of `esp_log.txt` | one `GPU adapter [N] name, vendor 0x.. device 0x.., X MB dedicated[, software][, drives the primary monitor]` line per adapter, then `GPU adapter: <what>. Why: <reason>.` |
| **hybrid laptop** (Intel/AMD iGPU + NVIDIA dGPU), default settings | the adapter marked "drives the primary monitor" is chosen (normally the iGPU) with the reason; the overlay draws over the game even when the game runs on the dGPU. If the overlay is missing or slow: try `--adapter 0` and `--adapter 1` and send both logs |
| laptop with a MUX switch set to discrete-only | the dGPU is the one that drives the monitor; same reasoning applies |
| `mcesp --adapter nvidia` / `--adapter 1` | `chosen with --adapter ...`; a wrong value is ignored and said so |
| a **VM or remote desktop session** with no GPU | `only software adapters exist`, then `Running on the WARP software rasteriser` (or the legacy renderer if even that fails). WARP drawing a DirectComposition overlay is expected to work but is **untested**; if the overlay is missing, `--gdi` is the CPU alternative |

Known limit: the adapter is chosen once at start-up from the **primary** monitor. If the game runs on a secondary monitor that is driven by a different GPU, choose the adapter by hand (`--adapter`).
