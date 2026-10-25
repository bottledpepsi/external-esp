// Wire.h - parser for the agent's UDP datagrams (the format is documented in docs/wire.md and agent/src/mcradar/Snapshot.java).
//
//   MCRADAR 4                                                           (v4 = v3 + the T and F lines below)
//   K <token>                                                           v5: the session token the overlay gave the agent (see Token.h)
//   Q <session> <seq>                                                   v5: agent session (8 hex) and datagram number, for loss / reorder accounting
//   T <nanoTime>                                                        v4: instant the agent read the game state (agent clock, ns)
//   S <status text>
//   C <x> <y> <z> <yaw> <pitch> <fov|-> <mode>                          the game camera (mode 1 first / 2 third-back / 3 third-front)
//   L <name> <x> <y> <z> <yaw|-> <pitch|-> <health|-> <w|-> <h|-> <uuid|->   local player
//   P <name> ...same columns...                                         other players
//   F 1                                                                 v4: the item list follows (omitted => keep the previous items)
//   I <x> <y> <z> <w|-> <h|-> <count> <key> <ench|-> <tags|->           dropped item; key/ench/tags percent-encoded
// "MCRADAR 1" (yaw only), "MCRADAR 2" (+ pitch, health) and "MCRADAR 3" are still accepted. '-' = unknown.
//
// The datagram comes from another process, so the parser treats it as untrusted: numbers must be plain finite decimals inside sane
// ranges, counts and string lengths are capped, text is forced to valid UTF-8 without control characters. See "Limits" in docs/wire.md.
#pragma once
#include "Types.h"
#include <cstddef>

namespace wire
{
    // ---- protocol versions this parser understands ----
    inline constexpr int kMinVersion = 1;
    inline constexpr int kTimestampVersion = 4; // first version with the T sample stamp: older agents work, but the smoothing is degraded
    inline constexpr int kMaxVersion = 5; // what the overlay tells the agent it can read (agent option wire=N); v4 = T and F lines, v5 = + K (session token)

    // ---- limits (docs/wire.md) ----
    inline constexpr size_t kMaxPlayers = 256; // P lines kept per datagram
    inline constexpr size_t kMaxItems = 1024; // I lines kept per datagram
    inline constexpr size_t kMaxEnchants = 32; // per item
    inline constexpr size_t kMaxNameBytes = 64;
    inline constexpr size_t kMaxUuidBytes = 40;
    inline constexpr size_t kMaxStatusBytes = 256;
    inline constexpr size_t kMaxKeyBytes = 128;
    inline constexpr size_t kMaxEnchantTextBytes = 1024;
    inline constexpr size_t kMaxTagsBytes = 4096;
    inline constexpr size_t kMaxTokenBytes = 64;
    inline constexpr double kMaxCoord = 1.0e8; // |x|, |y|, |z|: the world border is 3e7
    inline constexpr double kMaxAngle = 1.0e6; // degrees (yaw is unwrapped and can accumulate)
    inline constexpr double kMaxHealth = 1.0e5;
    inline constexpr double kMaxBox = 64.0; // hitbox / item box width and height, exclusive of 0
    inline constexpr double kMinFov = 1.0, kMaxFov = 179.0; // outside this the FOV is treated as unknown
    inline constexpr int kMaxCount = 1000000; // item stack size is clamped to 1..kMaxCount

    // Forces `s` to valid UTF-8 (each bad byte sequence becomes U+FFFD), replaces control characters (< 0x20, 0x7F) with '?' and cuts the
    // result at a character boundary so it is at most `maxBytes` long.
    std::string CleanText(const std::string& s, size_t maxBytes);
} // namespace wire

enum class ParseStatus
{
    Ok,
    NotOurs, // does not start with "MCRADAR <n>" (some other program talking to the port, or garbage)
    UnsupportedVersion, // "MCRADAR <n>" with n outside kMinVersion..kMaxVersion: an agent from a different build
};

struct ParseResult
{
    ParseStatus status = ParseStatus::NotOurs;
    int version = 0; // the n of the header when there was one (also for UnsupportedVersion), else 0
};

// The same as ParseDatagram, but says WHY a datagram was refused, so the overlay can tell the user "agent speaks v6, rebuild".
ParseResult ParseDatagramEx(const char* data, size_t len, WorldSnapshot& out);
inline bool ParseDatagram(const char* data, size_t len, WorldSnapshot& out) { return ParseDatagramEx(data, len, out).status == ParseStatus::Ok; }
