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
