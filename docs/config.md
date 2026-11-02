# Configuration files

## Where they live

| File | Folder |
|---|---|
| `esp_config.json`, `friends.json`, `profiles\<name>.json` | `%APPDATA%\external-esp` (usually `C:\Users\<you>\AppData\Roaming\external-esp`) |
| `esp_log.txt`, `esp_log.1.txt`, `esp_agent.log`, `esp_layout.json` ... | next to the exe / the jar (unchanged) |

The folder name is the single constant `paths::kAppFolderName` in `src/Paths.h`. If `%APPDATA%` is not set (a stripped-down environment) the settings stay next to the exe.

## Moving from an older build

On the first start with an empty `%APPDATA%\external-esp`, the program copies `esp_config.json`, `friends.json` and `profiles\*.json` from the exe's folder. Nothing is moved or deleted, so an older exe keeps working with its own files, and nothing in the new folder is ever overwritten.
The import happens once: a marker file `.imported-from-exe-folder` in the new folder records it, so deleting your config to start over does not bring the old one back. Delete the marker as well if you want to import again.

The diagnostics report (`--diagnostics`, "Copy diagnostics") shows this folder as `<config dir>`, not with your Windows user name.

## The file format and what is checked

`esp_config.json` (and every `profiles\<name>.json`) is one JSON object. The first key is `"Version"` (currently **1**). Every other key is optional: a missing key has its default (the defaults are the initialisers in `src/EspConfig.h`).

On load each key is checked against `src/ConfigSchema.cpp`:

| Problem | What happens | What you see in `esp_log.txt` |
|---|---|---|
| wrong type (`"BoxThickness": "thick"`) | the default is used | `BoxThickness: expected a number, found text - using the default` |
| number outside the range (`"NameSize": 99`) | clamped to the nearest limit | `NameSize: 99 is outside 10..17 - using 17` |
| a word that is not allowed (`"BoxStyle": "Fancy"`) | the default is used | `BoxStyle: 'Fancy' is not one of Rounded, Sharp, Corner-only, Skeleton - using Sharp` |
| not a key name (`"HotkeyConfig": "Banana"`) | the default is used | `HotkeyConfig: 'Banana' is not a key name ...` |
| `-inf`, `nan`, `1e999` | treated as "not a number" | as for a wrong type |
| a key this build does not know | **kept** and written back on the next save | - |
| a file from a newer build (`"Version": 7`) | read as well as possible, unknown keys kept, saved as version 1 | one warning |
| the file is not a JSON object | defaults | `not a JSON object: ...` |

A value between two of the UI's steps (for example `"MinimapSize": 175`) is not an error: the UI works with preset steps, and the nearest one is used.

## Versions and migration

`configschema::kCurrentVersion` is the layout version. A file without `"Version"` (the C# tool's, or a build before this one) is version 0 and goes through the same migration path as any later upgrade: `Migrate()` runs one function per step, `0 -> 1`, `1 -> 2`, ... in order, then the keys are checked. The current table has one step (`0 -> 1`; the keys mean the same, so it only records that the file was unversioned).

When you rename, remove or change the meaning of a key: bump `kCurrentVersion` and add the step to `Steps()` in `ConfigSchema.cpp`. Adding a key with a default needs no bump. `tests/config_schema_test.cpp` fails if a key is added to `EspConfig` without a spec, or if a step is missing from the chain.

## Saving and recovery

Every settings file is written the same way (`src/ConfigStore.cpp`): the new text goes to `<file>.tmp` and is flushed to the disk, the previous file - if it is valid - is copied to `<file>.bak`, and then `<file>.tmp` replaces `<file>` in one step (`MoveFileEx`, retried briefly if a virus scanner holds the file). A crash or power loss leaves either the complete old file or the complete new one.

On load, a file that is missing is a first run (defaults). A file that is empty, cut off, not JSON, or not the right kind is **not trusted**: it is renamed to `esp_config.corrupt-<date>-<time>.json` (the newest three are kept), `esp_config.json.bak` is restored if it is valid, and the log says which happened:

    esp_config.json was damaged (not valid JSON); kept as esp_config.corrupt-20261031-011318.json; the last good copy was restored

A file that exists but cannot be opened (locked, no permission) is left alone and the defaults are used for that run.

The same applies to `friends.json`. Profiles are written atomically but have no `.bak`.

The settings window does not write on every click: changes are collected and written once they stop for 0.4 s (at the latest 3 s after the first), when the window closes, and on exit.

## Profiles

A profile is a saved copy of all settings: `profiles\<name>.json` (same format and checks as `esp_config.json`). The PROFILES tab saves, loads, deletes and shows the **active** profile - the one most recently loaded or saved (kept in `active_profile.txt`; it is shown, and is what UPDATE overwrites). NEXT switches to the next profile in the list.

The first start creates three profiles in an empty `profiles\` folder, once (`.builtins-seeded` stops them coming back if you delete them; an existing profile of the same name is never overwritten):

| Profile | What it is |
|---|---|
| `default` | the program's defaults |
| `testing` | everything visible and described: item tags, enchants, health, minimap, tracers; no distance limits, junk not hidden |
| `low-power` | 60 fps cap, vsync, no minimap or tracers, no text shadow, no item extras |

Start with a profile: `mcesp --profile low-power`. It becomes the current settings (written to `esp_config.json`). An unknown name prints the list of profiles and exits with code 2.
