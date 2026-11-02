# Command line

(`mcesp --help` prints the options; this page has what does not fit there.)

## Exit codes

| Code | Meaning |
|---|---|
| 0 | success |
| 1 | something failed while running (graphics could not start, an unexpected error) |
| 2 | wrong command line (unknown option, missing or bad value, unknown profile) |
| 10-16 | `--selftest` outcomes, see docs/selftest.md (16 = the UDP port is in use, also outside the self-test) |
| 17 | another mcesp is already running on this port |
| 18 | no usable agent jar |

The values live in `src/ExitCodes.h`; a test checks they do not collide with the self-test's.

## One overlay per port

Starting a second `mcesp` on the same UDP port prints who has it and exits with 17:

    mcesp is already running (process 1234) on port 47615.
      - Close it first (hold the exit hotkey, or close its console), or
      - to run a second client on this PC, start this one with  --port <another port> --pid <the game's process id>.

Two clients on one PC use two ports and are independent. `--demo` and `--replay` take no port and are not checked.
