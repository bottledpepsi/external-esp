# Changelog

Newest first. Only behaviour a user or a bug report can notice is listed; see `git log` for the rest.

## Unreleased (Phase 2: compatibility and robustness)

- **Fixed:** restarting `mcesp.exe` while Minecraft kept running left the overlay without data until the game was restarted. The running agent ignored the
  second load, so it kept sending the old session token to the old port. A second load now reconfigures the running agent (port, rate, token, wire version,
  verbose) and restarts its worker thread if that had died. The agent log says what changed (never the token).
- **Added:** agent option `dump=1` writes `esp_structure.json`, the structure (never values) of the classes discovery relied on, for bug reports and for
  rebuilding a fake game from a real version (docs/fixtures.md).
