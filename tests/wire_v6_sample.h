// The v6 sample datagram: exactly what the Java encoder (Snapshot.encode, wire 6) writes for the "full" sample plus three entities (a zombie, a cow and an
// arrow whose id is the largest 32-bit value), token below, session abcdef01, number 7. A literal on BOTH sides - here and in
// agent/test/src/mcradar/SnapshotTest.java (V6_FULL) - for the same reason as tests/wire_v5_sample.h. If you change the v6 format, change BOTH literals.
#pragma once

inline constexpr const char* kV6Full =
    "MCRADAR 6\n"
    "K 0123456789abcdef0123456789abcdef\n"
    "Q abcdef01 7\n"
    "T 123456789012345\n"
    "S agent ok: 1 other player(s)\n"
    "C 97.9276 65.6200 -45.4497 -175.83 18.91 68.02 2\n"
    "L Alice 100.000 64.000 -50.000 12.50 -3.25 17.50 0.60 1.80 069a79f4-44e9-4726-a5be-fca90e38aaf5\n"
    "P Bob 90.500 70.000 -60.250 12.50 -3.25 17.50 0.60 1.80 069a79f4-44e9-4726-a5be-fca90e38aaf5\n"
    "F 1\n"
    "I 103.000 64.000 -47.000 0.25 0.25 1 minecraft:diamond_sword minecraft:sharpness@5,minecraft:unbreaking@3 %7BEnchantments:%5B%7Bid:%22minecraft:sharpness%22%7D%5D%7D\n"
    "I 98.000 64.000 -52.000 - - 37 minecraft:cobblestone - -\n"
    "E 1234567 1 92.500 64.000 -47.250 0.60 1.95 20.00 Zombie\n"
    "E 77 2 99.000 64.000 -44.000 0.90 1.40 10.00 Cow\n"
    "E 4294967295 4 97.000 66.500 -48.000 0.50 0.50 - Arrow\n";
