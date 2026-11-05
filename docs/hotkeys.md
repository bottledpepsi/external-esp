# Hotkeys

A hotkey is a key, optionally with **Ctrl, Shift, Alt, Win**: `F6`, `Ctrl+Shift+F5`. In `esp_config.json` it is a string (`"HotkeyToggle": "Ctrl+Shift+F5"`); in the HOTKEYS tab, select a button and press the keys (hold the modifiers while you press the key). `Esc` cancels. A modifier alone cannot be a hotkey.

| Action | Key in the file | Default |
|---|---|---|
| Toggle overlay | `HotkeyToggle` | Delete |
| Open config | `HotkeyConfig` | Insert |
| Hold to exit (3 s) | `HotkeyExit` | End |
| Toggle players | `HotkeyTogglePlayers` | not set |
| Toggle items | `HotkeyToggleItems` | not set |
| Toggle tracers | `HotkeyToggleTracers` | not set |
| Toggle minimap | `HotkeyToggleMinimap` | not set |
| Next profile | `HotkeyNextProfile` | not set |
| Cycle status text (Off / Compact / Detailed) | `HotkeyCycleHud` | not set |
| Cycle raw / smoothed view (Smoothed / Raw / Both) | `HotkeyCycleView` | not set |
| Toggle performance graphs | `HotkeyToggleGraphs` | not set |

The new actions have no default key on purpose: any key we pick could be one the game or a server uses. Bind them in the HOTKEYS tab (CLEAR unbinds). The two that open and hide the overlay cannot be left empty. The category toggles change the setting itself, so they are saved like a click in the window; "Next profile" works like NEXT in the PROFILES tab.

## Rules

- **A key can only do one thing.** Binding a key that another action uses is refused in the tab ("already used by ..."); a file that has the same key twice is read with the earlier action keeping it, a warning in the log, and the later optional action unbound (a required one goes back to its default).
- **Windows' own combinations are refused** (Alt+F4, Alt+Tab, Alt+Esc, Alt+Space, Ctrl+Alt+Delete, Win+L, Ctrl+Esc).
- Modifiers must match exactly: `Delete` does not fire while Ctrl is held; `Ctrl+Delete` does not fire without it.
- Key names: `A`-`Z`, `0`-`9`, `F1`-`F24`, `Delete`, `Insert`, `Home`, `End`, `PageUp`, `PageDown`, `Space`, `Tab`, `Enter`, `Backspace`, `Left`/`Up`/`Right`/`Down`, `Numpad0`-`Numpad9`, `NumpadAdd`..., `Tilde`, `[`, `]`, ... or any virtual-key code as `0xNN`.
- Hotkeys only act while Minecraft (or the overlay) has focus.

## System hotkeys, and what they take from other programs

Hotkeys are registered with Windows (`RegisterHotKey`), so a press is never missed during a slow frame and the key does not also reach the game. The price is that **a registered key is taken from every program**. Therefore:

- keys are registered **only while Minecraft or the overlay has focus**, and released the moment focus goes (alt-tab to a browser and Delete is the browser's again);
- while the HOTKEYS tab is waiting for a key (rebinding), nothing is registered, so every key reaches the window;
- the "hold to exit" key is never registered (it needs "held for 3 s"); it is polled and also reaches the game;
- if another program already owns a combination, Windows refuses it. You get a line in the HOTKEYS tab (`Delete: taken by another program, polled instead`) and in `esp_log.txt` (once), and that action works by polling the key instead - the game then also sees the key. The registration is retried every 5 s and on each focus change.
