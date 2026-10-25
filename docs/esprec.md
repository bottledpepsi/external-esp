# The `.esprec` recording format

An `.esprec` file is the agent's **raw UDP datagrams plus the time each one arrived**, nothing else. Replaying it feeds the
same bytes to the same parser at the same moments, so the overlay (or a test) sees what it saw during the session, without the
game. Implementation: `src/EspRec.h`, `src/EspRec.cpp`. Tests: `tests/esprec_test.cpp`.

All integers are **little-endian**, fixed width, no padding.

## Layout

```
file   = header record*
```

### Header (32 bytes at format version 1)

| offset | size | field            | meaning |
|-------:|-----:|------------------|---------|
| 0      | 8    | `magic`          | `45 53 50 52 45 43 1A 0A` = `"ESPREC"`, `Ctrl-Z`, `LF` (the PNG trick: text-mode transfers and "is this text?" sniffers damage it visibly) |
| 8      | 2    | `format_version` | `1`. Bumped only for an **incompatible** change |
| 10     | 2    | `header_size`    | bytes from the start of the file to the first record. `32` today; may grow |
| 12     | 4    | `flags`          | none defined; readers ignore unknown bits |
| 16     | 4    | `wire_version`   | `MCRADAR` version of the first datagram (`4`, `5`, ...), `0` = unknown. Informational |
| 20     | 4    | `reserved`       | `0` |
| 24     | 8    | `start_unix_ms`  | wall-clock time (UTC, ms since 1970) when recording started. Informational |

### Record (16 bytes + payload)

| offset | size | field     | meaning |
|-------:|-----:|-----------|---------|
| 0      | 8    | `t_ns`    | arrival time in nanoseconds since the start of the recording, from a monotonic clock. Non-decreasing |
| 8      | 4    | `length`  | payload size in bytes, `0 .. 1 048 576` (a UDP datagram is < 64 KB; larger means corruption) |
| 12     | 4    | `kind`    | `0` = one raw datagram exactly as `recvfrom` returned it. Other values are reserved |
| 16     | n    | `payload` | `length` bytes |

`t_ns` is the **arrival** time at the socket (what the Smoother's fallback clock uses), not the agent's `T` stamp (that one
is inside the payload and is replayed untouched).

## Compatibility rules

A reader for format version 1 must:

* accept `format_version == 1` and refuse `0` and anything newer (`UnsupportedVersion`);
* start reading records at `header_size` (skip header bytes it does not understand); `header_size < 32` is corrupt;
* ignore unknown `flags` bits;
* **skip** records whose `kind` is not `0` (counted, not an error);
* treat `length > 1 048 576` as corruption and stop;
* clamp a `t_ns` that goes backwards to the previous value (counted).

A writer must only append, and must never change the meaning of an existing field within a format version.

There is **no footer and no record count**. A recording that was cut off (crash, kill, full disk) is readable up to its last
complete record; a partial last record is reported as `Truncated` and dropped. Replay treats `Truncated` as a normal end.

## Sizes

About 100-250 bytes of framing per second at 120 Hz (16 bytes per datagram) on top of the datagrams, which are a few hundred bytes
to a few KB each (the item list, every ~100 ms, dominates). Expect single-digit MB per minute. There is no compression in
format version 1; a compressed variant would use a `flags` bit or a new `format_version`.

## Inspecting a file

```
xxd -l 64 session.esprec        # magic, version, first record header
```

## Privacy

Datagrams contain player names, UUIDs, coordinates and item data from the game. From wire v5 they also contain the session
token (see the wire documentation). A recording is a record of the session: do not post one publicly without looking at it, and prefer
`--diagnostics`'s redacted output for bug reports.
