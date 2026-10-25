// The v5 sample datagram: exactly what the Java encoder (Snapshot.encode, wire 5) writes for the "full" sample snapshot (token below, session abcdef01, number 7).
// It is a literal on BOTH sides - here and in agent/test/src/mcradar/SnapshotTest.java (V5_FULL) - instead of a committed file, because the file was
// the one thing in CI that kept arriving altered (patch tools, line-ending conversion). The Java test fails if the encoder stops producing it; the C++
// tests parse it. If you change the v5 format, change BOTH literals (the tests in each language point at the other).
#pragma once

inline constexpr const char* kV5FullToken = "0123456789abcdef0123456789abcdef";
inline constexpr const char* kV5Full =
    "MCRADAR 5\n"
    "K 0123456789abcdef0123456789abcdef\n"
    "Q abcdef01 7\n"
    "T 123456789012345\n"
    "S agent ok: 1 other player(s)\n"
    "C 97.9276 65.6200 -45.4497 -175.83 18.91 68.02 2\n"
    "L Alice 100.000 64.000 -50.000 12.50 -3.25 17.50 0.60 1.80 069a79f4-44e9-4726-a5be-fca90e38aaf5\n"
    "P Bob 90.500 70.000 -60.250 12.50 -3.25 17.50 0.60 1.80 069a79f4-44e9-4726-a5be-fca90e38aaf5\n"
    "F 1\n"
    "I 103.000 64.000 -47.000 0.25 0.25 1 minecraft:diamond_sword minecraft:sharpness@5,minecraft:unbreaking@3 %7BEnchantments:%5B%7Bid:%22minecraft:sharpness%22%7D%5D%7D\n"
    "I 98.000 64.000 -52.000 - - 37 minecraft:cobblestone - -\n";
