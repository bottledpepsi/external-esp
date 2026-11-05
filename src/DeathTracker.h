// DeathTracker.h - notices that the local player died and keeps a marker where it happened, through the death screen and the respawn.
//
// Death is read from the local player's health only: health <= 0 for kConfirmSec in a row means dead (a single 0 while a world loads does not count).
// The marker is the position at the first dead sample. It stays after the respawn - that is when it is useful - until the player gets within
// kReachDist blocks of it (at least kArriveGraceSec after the respawn), kExpireSec pass, Clear() is called, or the next death replaces it.
// Without a health reading nothing is ever concluded; a teleport (ender pearl, /tp) is not a death.
//
//   Update(nowSec, healthValid, health, posValid, pos)   once per frame with the local player
//   HasMarker() / Marker()                               where to draw
//   Dead()                                               true from the confirmed death until the health is above 0 again
//   Deaths()                                             how many deaths were noticed since Clear() of the whole tracker (Reset())
//
// Portable (no Windows headers).
#pragma once
#include "Types.h"
#include <cmath>

class DeathTracker
{
public:
    static constexpr double kConfirmSec = 0.25;
    static constexpr double kReachDist = 3.0; // horizontal blocks
    static constexpr double kArriveGraceSec = 1.0; // after the respawn: standing next to the old spot (bed) does not clear it at once
    static constexpr double kExpireSec = 600.0;

    void Reset()
    {
        *this = DeathTracker();
    }
    void Clear() { has_ = false; }
    bool HasMarker() const { return has_; }
    const Vec3& Marker() const { return marker_; }
    bool Dead() const { return dead_; }
    int Deaths() const { return deaths_; }
    double DiedAt() const { return diedAt_; }

    void Update(double now, bool healthValid, float health, bool posValid, const Vec3& pos)
    {
        if (!healthValid) return; // nothing to conclude (keeps whatever state it had)
        if (has_ && now - diedAt_ > kExpireSec) has_ = false;
        if (!dead_)
        {
            if (health <= 0.f)
            {
                if (zeroSince_ < 0)
                {
                    zeroSince_ = now;
                    zeroPos_ = posValid ? pos : lastAlive_;
                    zeroPosKnown_ = posValid || lastAliveKnown_;
                }
                if (now - zeroSince_ >= kConfirmSec && zeroPosKnown_)
                {
                    dead_ = true;
                    has_ = true;
                    marker_ = zeroPos_;
                    diedAt_ = zeroSince_;
                    ++deaths_;
                }
            }
            else
            {
                zeroSince_ = -1;
                if (posValid)
                {
                    lastAlive_ = pos;
                    lastAliveKnown_ = true;
                }
                if (has_ && posValid && now - respawnedAt_ > kArriveGraceSec && Horizontal(pos, marker_) < kReachDist) has_ = false; // got there
            }
        }
        else if (health > 0.f)
        {
            dead_ = false; // respawned: the marker stays
            zeroSince_ = -1;
            respawnedAt_ = now;
            if (posValid)
            {
                lastAlive_ = pos;
                lastAliveKnown_ = true;
            }
        }
    }

private:
    static double Horizontal(const Vec3& a, const Vec3& b) { return std::hypot(a.x - b.x, a.z - b.z); }

    bool has_ = false, dead_ = false;
    Vec3 marker_;
    double diedAt_ = 0, respawnedAt_ = -1e9, zeroSince_ = -1;
    Vec3 zeroPos_, lastAlive_;
    bool zeroPosKnown_ = false, lastAliveKnown_ = false;
    int deaths_ = 0;
};
