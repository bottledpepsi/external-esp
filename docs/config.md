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
