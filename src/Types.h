// Types.h - data shared between the UDP thread and the UI/render thread.
#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

struct Vec3
{
    double x = 0, y = 0, z = 0;
};

// Minecraft conventions: +X east, +Y up, +Z south. yaw 0 = facing +Z, increasing clockwise seen from
// above (so yaw 90 faces -X / west); pitch +90 = looking straight down.
struct PlayerState
{
    Vec3 pos; // feet position
    float yawDeg = 0, pitchDeg = 0, health = 0;
    bool yawValid = false, pitchValid = false, healthValid = false;
    float width = 0.6f, height = 1.8f; // hitbox from the game (sneaking players are shorter)
    bool sizeValid = false;
    std::string name;
    std::string uuid; // "" if unknown; online-mode UUIDs are version 4, offline ones version 3
};

struct Enchant
{
    std::string id; // "minecraft:sharpness" (or a legacy numeric id)
    int level = 1;
};

// Dropped item entity with its stack contents.
struct ItemState
{
    Vec3 pos;
    float width = 0.25f, height = 0.25f;
    bool sizeValid = false;
    int count = 1;
    std::string key; // "minecraft:diamond_sword"
    std::vector<Enchant> enchants;
    std::string tags; // the stack's NBT / component text (SNBT), may be long
};

// The game's camera as read by the agent. `fovValid` is false when the FOV method could not be found
// (the configured FOV is used then). mode: 1 first person, 2 third person (behind), 3 third person (front).
// What kind of thing a wire-v6 "E" record is. The agent decides from the entity's class (its name and its superclass names); `Other` = a living
// thing it could not place. The overlay's category filters and colors are keyed by this (EspConfig / EspRenderer).
enum class EntityKind : uint8_t
{
    Other = 0, // a mob / creature that matched none of the below (grey, hidden by default)
    Hostile = 1,
    Passive = 2,
    Neutral = 3,
    Projectile = 4,
    Xp = 5, // experience orbs
    Count
};
inline constexpr int kEntityKindCount = static_cast<int>(EntityKind::Count);

// A non-player, non-item entity (wire v6 "E" line). `id` is the agent's stable id for the object while it lives (identity hash), so the overlay can
// follow one entity from sample to sample; it can repeat after the object is gone and is never an index.
struct EntityState
{
    uint32_t id = 0;
    EntityKind kind = EntityKind::Other;
    Vec3 pos;
    float width = 0.6f, height = 1.8f;
    bool sizeValid = false;
    float health = 0;
    bool healthValid = false;
    std::string label; // the class's simple name ("Zombie"), may be empty
};

struct CameraState
{
    bool valid = false;
    Vec3 pos;
    float yawDeg = 0, pitchDeg = 0, fovDeg = 70;
    bool fovValid = false;
    int mode = 1;
};

struct WorldSnapshot
{
    bool localValid = false;
    PlayerState local;
    CameraState camera;
    std::vector<PlayerState> players; // OTHER players only
    std::vector<ItemState> items;
    std::vector<EntityState> entities; // wire v6; sent with the item list (hasItems), so the overlay keeps the previous ones when a datagram has neither
    std::string status; // agent status line
    uint64_t seq = 0; // increments per received datagram
    std::chrono::steady_clock::time_point arrived{};
    // Wire v4: the instant the agent read the game state (its System.nanoTime(), a clock the Smoother maps onto
    // ours). Lets the overlay ignore UDP / thread-wake-up jitter. Absent (false) for older agents and the demo.
    bool hasSampleTime = false;
    int64_t sampleNs = 0;
    // Wire v4: false = this datagram carries no item list (items are sent at ~10 Hz); keep the previous one.
    bool hasItems = true;
    // Number of lines with a known tag that were dropped by the parser (bad number, out of range, over the per-datagram limits).
    uint32_t ignoredLines = 0;
    // wire v5 Q line: the agent session this datagram belongs to and its running datagram number (1, 2, 3, ...; wraps at 2^32)
    bool hasWireSeq = false;
    uint32_t wireSession = 0, wireSeq = 0;
    std::string token; // wire v5: the K line (empty when absent); checked by StreamIngest, never used after that
    int protocolVersion = 0; // the "MCRADAR <n>" the datagram announced (0 for the demo)
};
