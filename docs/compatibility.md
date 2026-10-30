# Compatibility: what is claimed, what was verified, and how

Written at the end of "Phase 2". The rule used throughout: a thing is called verified only for the exact way it was checked; "synthetic" is never "works on version X".
Environment of the verification below: Linux sandbox, JDK 17 / 21 / 25 (OpenJDK), g++ 13, mingw-w64 11 cross-compiler (GCC 13, real Windows headers), Wine 9.0 + Xvfb + Mesa software GL.
**No Windows machine, no real GPU, no real Minecraft, no launcher was available.**

## Minecraft versions
| version | status | evidence |
|---|---|---|
| 26.1 (vanilla) | reported working by the project owner before Phase 2 (camera pose, item ESP, GPU renderer) | not reproducible here; **no dump from it exists in the repo** |
| every other version, Fabric/Forge/NeoForge | **unknown** | 0 real fixtures. Two synthetic fixtures (dumps of this repo's own fake games) prove the dump -> rebuilt game -> discovery round trip, nothing about a version |

How to change that: `mcesp --selftest` on a version, send `esp_selftest.txt` + `esp_structure.json`; drop the structure in `agent/test/fixtures/<version>/` with `real: true` (docs/fixtures.md).

## Java (the game's JVM and the build)
| | status | evidence |
|---|---|---|
| agent bytecode | Java 8 (`--release 8`) | built that way; run on 17/21/25 |
| Java unit + fixture + chain + regression + pacing tests | pass on JDK 17, 21, 25 | this environment (CI matrix 17/21/25 runs them) |
| real-jar smoke tests (10 cases incl. a second Attach, self-test, dump) | pass on JDK 21 and 25 | `agent_smoke.py`, real `VirtualMachine.attach` into fake games on Linux |
| JDK 8 | not run | the reload / self-test smoke cases skip below JDK 9 |

## Discovery (agent)
Chains with a named winner, per-class caching, negative caching reset on a world change, time budgets; all exercised through fixture variants (what each mechanism does is in
`CHANGELOG.md`; each was disabled in turn and its test failed). They are **insurance against structures nobody has seen**; whether a real version needs stage 2/3, the getter,
the relaxed camera match or an ancestor FOV method is unknown.

## Overlay / Windows
| area | verified | not verified |
|---|---|---|
| compiles | whole exe against real Windows headers (mingw-w64 11, GCC 13), clean under `-Wall -Wextra`; CI builds it with current MSYS2 GCC | the user's GCC 15.2 toolchain |
| process selection, inject retry policy, GPU recovery decisions, window classification, adapter choice, self-test report/exit codes, interval statistics | portable headers with unit tests (25 native tests) | their use against real processes, GPUs, windows |
| start-up, flags, logs, legacy renderer, UDP path, agent restart handling, session reset | under Wine + Xvfb with a Linux fake game as the data source | real Windows |
| GPU renderer (D3D11 + DirectComposition), device loss at run time, runtime switch to GDI | compile only (Wine has no DirectComposition: it falls back to the legacy renderer) | everything at run time: `docs/windows-checklist.md` items 1, 4 |
| per-monitor DPI, multi-monitor | compile + logging + fallback chain under Wine | real scaling, a second monitor |
| fullscreen / z-order | a probe with stand-in windows under Wine (9 checks) | real exclusive fullscreen, GLFW fullscreen, Windows 10/11 z-order, DWM cloaking |
| timer resolution / pacing | tools and statistics; JDK sources read (docs/pacing.md) | **any** Windows timing number |
| launchers (vanilla, Prism, MultiMC, ATLauncher, Lunar, ...) | the selection rules on hand-made process lists | any real launcher |
| attach on Windows | the Attach code on Linux; failure hints match this code's own messages | a real Windows attach, the hint texts against real failures |

## Behaviour changes worth knowing
* Client selection is explicit: several clients, or no window titled "Minecraft" with other Java windows around, are **refused** (with a list), not guessed. `--pid`, `--title`, `--exe` choose.
* A failed attach is retried with growing delays instead of every 30 s.
* The overlay drops all state when the game or agent restarts.
* GPU device loss now ends in the GDI renderer instead of retrying forever.
