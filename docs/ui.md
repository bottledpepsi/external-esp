# The settings window

Open it with the "Open config" hotkey (Insert by default) or `--open-config`. Everything in it works with the mouse **and** the keyboard.

## Mouse

Click a row to cycle it forward, right-click to cycle back, click a switch to flip it, drag the title bar to move the window, wheel to scroll.

## Keyboard

| Key | Does |
|---|---|
| Up / Down, Tab / Shift+Tab | previous / next control (Tab wraps; Up/Down stop at the ends) |
| Home / End, PgUp / PgDn | first / last control, 5 controls up / down |
| Left / Right | a cycle row goes back / forward, a switch goes off / on |
| Enter / Space | press the button, flip the switch, cycle forward, start typing in a text field |
| Delete | reset the control to its default (where it has one) |
| Ctrl+Tab, Ctrl+Shift+Tab (or Ctrl+PgDn / Ctrl+PgUp) | next / previous tab |
| Esc | close the window; in a text field or while rebinding a key it only cancels that |

A ring appears around the focused control after the first key press; moving the mouse hides it again. The first key press only shows the ring (it never changes a control you cannot see). The list scrolls to keep the focused control in view. While you type in a text field or a key is being rebound, the keys go to that and not to the navigation.

## The ADVANCED tab

Sliders for the numbers in docs/tuning.md: four smoothing values, the agent rate, the minimum item and player marker sizes and the label distance. Each row shows the current value (orange when it is not the default), a short help line, and a RESET button that appears only when the value was changed; **Delete** does the same from the keyboard, and RESET ALL TO DEFAULTS sits at the bottom. Drag with the mouse (the drag keeps following the cursor outside the track) or use Left / Right for one step.

The line under the smoothing sliders is a live measure: how far each new camera sample was from the prediction, in degrees and in screen pixels, averaged over the last ~20 samples. That is the size of the jump the "Correction blend" has to hide, so it is the number to watch while you move a slider: lower is better. The smoothing values take effect immediately; the changes are saved once you stop moving them.

## The status text (top-left of the screen)

GENERAL tab, "Status text" (`"HudMode"` in the file; the "Cycle status text" hotkey switches it): **Compact** (default), **Detailed**, **Off**.

**Compact** is one line while all is well:

    external-esp  |  active  |  262 fps  |  118 Hz  |  age 6 ms  |  loss 0.0%

`118 Hz` is how many samples per second reach the overlay; `age` is how old the newest sample is when a frame is drawn; `loss` is the share of the agent's numbered datagrams that never arrived over the last quarter second window. A second line (in orange when it is a problem) appears only when something needs attention: waiting for the game, UDP loss or reordering, a protocol mismatch, recording.

**Detailed** adds render time (`ms cpu`, `gpu`/`gdi`), the camera mode, the agent's own status, and a line `agent 118 Hz | data age 6.4 ms | jitter 0.8 ms | lost 7 (0.1%) | reordered 2 | guess off 1.5 px`:

- *data age* - how old the sample you are looking at is (the stamp the agent put on it, mapped to the overlay's clock; the loopback transport itself is microseconds). It cannot go below half the sample period, because samples are discrete.
- *jitter* - how much later than its best case a sample arrives, on average. Clock offset cancels out (the best case of the last ~5 s is the reference); it is a measure of scheduling noise, not of absolute latency, which cannot be measured across two clocks.
- *guess off* - the ADVANCED tab's prediction error, in screen pixels.

The text is rebuilt four times a second, not every frame, so it costs no allocation and the text cache keeps hitting. **Off** hides everything except the exit countdown when you hold the exit key.
