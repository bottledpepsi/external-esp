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
