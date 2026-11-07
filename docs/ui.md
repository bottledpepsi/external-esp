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

**Detailed** ends with a line about the overlay itself: `text cache  hit 99.2%  |  emptied 0 x  |  labels dropped 0`. *Hit* is how often a name, distance or other text came out of the text-layout cache instead of being laid out again during the last quarter second (the status text is refreshed four times a second); it sits near 100 % in normal use and the line turns orange below 80 %. *Emptied* counts the times a cache filled up and was cleared; a number that keeps growing means labels change every frame (a very large crowd, or text that contains a changing number). *Labels dropped* is how many labels the declutter pass (see docs/tuning.md) found no room for in the last frame.

**Compact** is one line while all is well:

    external-esp  |  active  |  262 fps  |  118 Hz  |  age 6 ms  |  loss 0.0%

`118 Hz` is how many samples per second reach the overlay; `age` is how old the newest sample is when a frame is drawn; `loss` is the share of the agent's numbered datagrams that never arrived over the last quarter second window. A second line (in orange when it is a problem) appears only when something needs attention: waiting for the game, UDP loss or reordering, a protocol mismatch, recording.

**Detailed** adds render time (`ms cpu`, `gpu`/`gdi`), the camera mode, the agent's own status, and a line `agent 118 Hz | data age 6.4 ms | jitter 0.8 ms | lost 7 (0.1%) | reordered 2 | guess off 1.5 px`:

- *data age* - how old the sample you are looking at is (the stamp the agent put on it, mapped to the overlay's clock; the loopback transport itself is microseconds). It cannot go below half the sample period, because samples are discrete.
- *jitter* - how much later than its best case a sample arrives, on average. Clock offset cancels out (the best case of the last ~5 s is the reference); it is a measure of scheduling noise, not of absolute latency, which cannot be measured across two clocks.
- *guess off* - the ADVANCED tab's prediction error, in screen pixels.

The text is rebuilt four times a second, not every frame, so it costs no allocation and the text cache keeps hitting. **Off** hides everything except the exit countdown when you hold the exit key.

## Item filters (ITEMS tab, "ITEM FILTER")

Three filters, all applied to the items the agent sends (so they cost nothing on the game side):

* **Hide junk items** (older toggle): the fixed junk list - rotten flesh, cobblestone, wooden tools, ...
* **Minimum value**: **All**, **No junk**, **Valuable**, **Rare**. Rare = netherite and diamond things, elytra, totems, enchanted golden apples, nether stars, beacons, tridents, dragon eggs and heads, hearts of the sea, shulker shells, conduits, ancient debris. Valuable = gold, emeralds, ender pearls and eyes, blaze rods, ghast tears, experience bottles, iron ingots and blocks, lapis, amethyst, obsidian, ender chests, shulker boxes, name tags, saddles, horse armor - and **anything enchanted**. Junk is never Valuable, except that an enchanted stack is not junk. Everything else is ordinary.
* **Name filter**: **Off**, **Whitelist** (only items matching a pattern) or **Blacklist** (everything except those). Patterns are typed in the field under it and added with ADD or Enter; Remove deletes one. `*` matches any run of characters, `?` one character. Case, spaces and underscores are ignored (`Diamond Sword`, `diamond_sword` and the old `diamondSword` are the same name). A pattern containing `:` is compared with the whole id (`mymod:ruby*`), any other with the part after the namespace. An empty whitelist shows nothing; an empty blacklist hides nothing.

File keys: `ItemMinTier` (0-3), `ItemFilterMode` (`Off`/`Whitelist`/`Blacklist`), `ItemFilterList` (text, comma separated, at most 512 characters and 64 patterns). The Detailed status text gets `items N shown, M filtered` when something is hidden.

## Raw / smoothed view (GENERAL tab "View", hotkey `HotkeyCycleView`)

The overlay normally draws the *smoothed* picture: the agent's samples, interpolated with the game's own tick timing and the camera extrapolated forward (see docs/tuning.md). To judge what the smoothing is doing, "View" can show the data underneath:

* **Smoothed** (default): the normal picture.
* **Raw**: the newest sample exactly as the agent sent it, projected with *its own* camera - no interpolation, no extrapolation, so every jump between samples is visible. Falls back to the smoothed picture while the sample has no camera. (With "Motion smoothing" off the two are the same.)
* **Both**: the smoothed picture as usual, plus the raw sample as thin magenta outlines (players) and small squares (items) under it. Where the two drift apart is what the smoothing is making up. No labels, tracers or health for the raw ones.

The status text says `RAW` or `RAW+SMOOTHED` while a raw view is on. The hotkey cycles Smoothed -> Raw -> Both and is not bound by default.

## Performance graphs and the jitter meter (GENERAL tab "Performance graphs", hotkey `HotkeyToggleGraphs`)

Off by default; while off nothing is recorded or drawn. On, the bottom-left corner shows two 240-frame graphs and a line of text:

* **Frame time** - the time between two frames in ms (this includes the wait for the display, so with VSync on a steady 60 Hz is a flat line at 16.7). Under it: average, 95th percentile and worst frame of the window, and what the overlay itself spent per frame (`cpu`). A frame is clipped to 500 ms in the graph.
* **Data age** - how old the newest agent sample was at the moment each frame was drawn, in ms: the latency of what you see. With a 20 Hz agent it is a sawtooth between 0 and 50.
* **Jitter** - the same measure the golden tests use (tests/golden.h), on the box of the nearest player who is on the screen: the distance of each frame's position from the midpoint of the two neighbouring frames, in pixels, as RMS / 95th percentile / maximum over the last 240 frames. Smooth motion gives about 0; a box that vibrates around a smoothly moving target gives a large number. "no player in view" while nobody qualifies (in front of the camera, at least 1.5 m away, inside the screen plus 10 %).

In a window at least 800 px high the panel also shows:

* **Wire**: the agent's datagrams in KB per second as a graph, with `wire 12.3 KB/s | 20.0 datagrams/s | 614 B each` under it (a window of a quarter second each; a replay counts what it plays back). A line that is flat at 0 while the game runs means no datagrams are arriving; a size that grows into the thousands of bytes means the entity / item lists are big.
* **Latency bar**: one bar split into *age* (how old the newest sample is when the frame is drawn), *frame* (the wait between frames) and *cpu* (the overlay's own time), with the sum and how often the agent sends: `latency 28.5 = age 20.0 + frame 8.0 + cpu 0.50 ms (agent every 50 ms)`. This is what you see lagging behind the game; the biggest piece is where to look.
* **Entities by category**: six bars (hostile, passive, neutral, projectile, xp, other) with the count the last frame drew, in the category colors; a category that is switched off is dim. Only while "Show entities" is on.

The scale of a graph steps through 4, 8, 16, 32 ... ms so it does not jump about with every frame; its top is written in the corner. The texts are rebuilt four times a second.

## Entities: mobs, projectiles, experience orbs (ITEMS tab, "ENTITIES")

Needs the agent to send wire v6 entities and class names it can read (docs/compatibility.md). **Off by default** (`ShowEntities`); nothing changes for an existing config until it is switched on.

* **Categories**: hostile mobs, passive mobs, neutral mobs, projectiles, experience orbs, and "other mobs" (a living thing the agent could not place; hidden by default). Each has its own switch (`ShowHostile`, `ShowPassive`, `ShowNeutral`, `ShowProjectiles`, `ShowXp`, `ShowOtherMobs`), its own color out of nine (`ColorHostile`, ... : Red, Orange, Yellow, Green, Cyan, Blue, Magenta, White, Gray) and its own hotkey (`HotkeyToggleHostile`, ...; pressing one while "Show entities" is off switches the master on too). `HotkeyToggleEntities` is the master switch.
* **What is drawn**: a box in the category's color; a health bar for living ones (when "Health bars" is on); for mobs, the class name and the distance, through the same fade and declutter as the players' labels. Projectiles and orbs get a small box only.
* **Distance** (`EntityMaxDist`: 32m, 64m (default), 128m, Unlimited). The agent sends the 128 nearest.
* **Smoothing**: mobs arrive about ten times a second. They are followed by the agent's id and moved ahead of their newest sample along the velocity of the last two (at most 0.15 s, never across a teleport), so a walking zombie does not step ten times a second. In the **Raw** view they are drawn as they arrived.
* The Detailed status text shows `mobs N/M shown`.

## Radar: orientation, corner, entities, death marker (MINIMAP tab)

* **Orientation** (`MinimapMode`): **North up** (default, as before: the map keeps its orientation and a wedge shows where you look) or **Rotate**: the map turns so what is in front of you is always at the top; an `N` on the rim says where north went.
* **Corner** (`MinimapCorner`): TopRight (default), TopLeft, BottomRight, BottomLeft.
* **Entities**: when the entity display is on, mobs and projectiles appear as small dots in their category color, only those inside the radar (they are not pinned to the rim).
* **Death marker** (`ShowDeathMarker`, default on; `HotkeyClearDeath` removes it): when your health reaches 0 and stays there for a quarter of a second, the place is remembered - a red cross on the radar and a red diamond with `Death  87m` in the world (at the screen edge when it is off to the side, at the bottom centre when it is behind you). It stays through the death screen and the respawn and goes away when you get within 3 blocks of it (a second after you respawn, so a bed next to it does not clear it at once), after ten minutes, on the hotkey, or when you die again. A single frame of 0 health while a world loads is not a death; a teleport is not a death; without a health reading from the agent nothing is concluded.

## Tracers: origin, distance, mobs (TRACERS tab)

* **Origin** (`TracerOrigin`): Bottom (default), Center or **Top** of the screen.
* **Max distance** (`TracerMaxDist`: 25m, 50m, 100m, Unlimited (default)): nothing farther away gets a line. This applies to players and mobs alike.
* **To players** (`TracerPlayers`, default on): the players' tracers, in their usual colors (friend / near-red to far-green / white). Turn it off to keep only the mobs' lines.
* **Tracers to mobs** (`TracerHostile`, `TracerPassive`, `TracerNeutral`, `TracerProjectiles`, `TracerXp`, `TracerOtherMobs`): one switch per category, in the category's own color (ENTITIES section). Only hostile mobs have one by default. A line needs the mob to be shown, so a category that is hidden or beyond the entity distance has no tracer; thickness, opacity, fade and target point are the ones set for the players' tracers.

## Themes (PROFILES tab, "THEMES")

A theme is the overlay's *look* as data: seven colors - enemy (player boxes, tracers, radar dots), friend, name text, distance text, item boxes, enchanted item boxes and the death marker - plus, optionally, a box style, a line width, an opacity and a font. The colors are ordinary settings (`ColorEnemy`, `ColorFriend`, `ColorText`, `ColorDistance`, `ColorItem`, `ColorItemEnchanted`, `ColorDeath`, written as `#RRGGBB` or `#RRGGBBAA`; `FontFamily`), so they are saved with the rest and carried by profiles. The default colors are exactly the ones used before themes existed.

* **Built in**: `default` (the colors only; applying it changes nothing else), `high-contrast` (yellow enemies, cyan friends, white text, sharp boxes, 3 px lines, fully opaque), `colorblind` (orange / blue instead of red / green), `soft` (muted, rounded, 1 px, 75 %).
* **Your own**: type a name, SAVE - the current look is written to `themes\<name>.json` in the settings folder. USE applies one; DEL removes it. Drop a theme file into that folder by hand and it appears in the list.
* **COPY JSON / PASTE**: the current look as JSON on the clipboard, and a theme from the clipboard applied at once. A theme of more than 16 KB is refused.
* **What applying does**: the colors are always set. A box style, line width (box and tracer thickness, rounded to the nearest preset), opacity (item and tracer opacity, nearest preset) or font in the theme is applied too; one that is missing leaves your setting alone. **A font applies after a restart** (the fonts are created at start-up); a font that is not installed falls back to the built-in list.

A theme file:

```
{ "name": "My theme", "enemy": "#FF2929", "friend": "#29FF59", "text": "#FFFFFF", "distance": "#D9E0F2E6",
  "item": "#FFFFFF", "itemEnchanted": "#9EC7FF", "death": "#FF4040",
  "boxStyle": "Sharp", "lineWidth": 2, "opacity": 0.75, "font": "Segoe UI" }
```

Every key is optional. A bad value (a color that is not `#RRGGBB[AA]`, an unknown box style, a line width outside 0.5-8, an opacity outside 0-1, a font name with control characters or over 64 characters) is reported and ignored without stopping the rest, and unknown keys are ignored. A theme file is only read as data. Names are made safe for a file name (letters, digits, space, `-`, `_`, `.`; no paths, no reserved Windows names).

## Entity inspector (hotkeys `HotkeyToggleInspector`, `HotkeyInspectAgent`)

Shows what the agent knows about one thing, and how it found it. Both hotkeys are unbound until you pick keys (HOTKEYS tab).

* **Inspect the entity at the crosshair** (`HotkeyToggleInspector`; press again to stop): the entity whose middle is nearest the centre of the screen (within 220 px, shown by the entity filters) is framed in white and a panel on the right lists, as the agent sees it:
  * `class` - the class and every superclass, which is what decides the category; `category`; `id` (the identity the E lines carry).
  * `position path`, `size path`, `health path` - *where in the game's objects each value is read from* (field names found by the agent), with what they read right now (`position`, `size`, `health`). A wrong-looking position or a missing health is answered here: the path is either not found (`-`) or it is not the one you expect.
  * Up to 30 of the entity's own fields (`Class.field : type` and the value): numbers, text, enum names, array lengths and the sizes of collections. Other game objects show their type only: the agent never calls a game object's `toString()`, because that would run game code.
* **Inspect how the agent found things** (`HotkeyInspectAgent`): the agent's discovery paths for the player list, the local player, the camera and the items, the wire version and rate, and the scan summary - the same information as `esp_agent.log`, live.
* Needs the agent to speak wire 6 and to have been given the control file (`ctl=`, passed automatically on attach and in the printed `-javaagent` line). Until it answers the panel says `waiting for the agent`. Not available while replaying (there is no agent).

**How it works.** The overlay writes a one-line request - `inspect <id>`, `inspect player` or `inspect off` - into `agent.ctl` in the settings folder and rewrites it every two seconds while it is inspecting; the file's modification time is the heartbeat. An agent that has not seen it renewed for six seconds stops (so a crashed overlay does not leave it inspecting), and an `inspect off` or a request for an id that is not in its entity list is answered at once. The answer travels in the normal datagram stream as `X` lines (docs/wire.md), only with the item / entity list and only while a request is live, so it costs nothing when the inspector is off. The agent only reads the request file; it never writes to the game.
