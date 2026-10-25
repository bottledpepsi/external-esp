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
    int protocolVersion = 0; // the "MCRADAR <n>" the datagram announced (0 for the demo)
};
