// EntityTracker.h - follows the wire-v6 entities (mobs, projectiles, orbs) from list to list so they move smoothly between the agent's samples.
//
// The agent sends the entity list with the items, about 10 times a second; drawn as they come, a walking zombie would jump a few pixels ten times a second.
// An entity is followed by its `id` (the agent's identity for the object). With two consecutive samples of the same id a velocity is known, and the
// position is extrapolated forward from the newest sample - never further than kMaxExtrapolation, so a stopped or vanished mob does not run away.
// No interpolation: it would show every mob a sample interval late.
//
//   Update(listSeq, list, tSec)  take a new list (ignored when `listSeq` is the one already taken - the Smoother keeps the old list between datagrams)
//   Predict(nowSec, out)         the entities at `nowSec` (out is cleared and refilled; its capacity is reused)
//
// Portable (no Windows headers). Entities that are not in the newest list are gone at once.
#pragma once
#include "Types.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

class EntityTracker
{
public:
    static constexpr double kMaxExtrapolation = 0.15; // seconds past the newest sample
    static constexpr double kMinInterval = 0.01, kMaxInterval = 1.0; // the time between two samples of one entity that gives a believable velocity
    static constexpr double kMaxJump = 6.0; // blocks between two samples: more is a teleport (or a reused id), not a speed

    void Clear()
    {
        tracks_.clear();
        seq_ = 0;
        have_ = false;
    }

    void Update(uint64_t listSeq, const std::vector<EntityState>& list, double tSec)
    {
        if (have_ && listSeq == seq_) return;
        have_ = true;
        seq_ = listSeq;
        next_.clear();
        for (auto& t : tracks_) t.used = false;
        for (const EntityState& e : list)
        {
            Track nt;
            nt.e = e;
            nt.t1 = tSec;
            nt.p1 = e.pos;
            for (Track& old : tracks_) // the first unused track with this id and kind (an id can repeat within a list: each takes its own)
            {
                if (old.used || old.e.id != e.id || old.e.kind != e.kind) continue;
                old.used = true;
                const double dt = tSec - old.t1;
                const double jump = Dist(old.p1, e.pos);
                if (dt >= kMinInterval && dt <= kMaxInterval && jump <= kMaxJump)
                {
                    nt.vel = {(e.pos.x - old.p1.x) / dt, (e.pos.y - old.p1.y) / dt, (e.pos.z - old.p1.z) / dt};
                    nt.hasVel = true;
                }
                break;
            }
            next_.push_back(std::move(nt));
        }
        tracks_.swap(next_);
    }

    void Predict(double nowSec, std::vector<EntityState>& out) const
    {
        out.clear();
        for (const Track& t : tracks_)
        {
            out.push_back(t.e);
            if (!t.hasVel) continue;
            const double dt = std::clamp(nowSec - t.t1, 0.0, kMaxExtrapolation);
            out.back().pos = {t.p1.x + t.vel.x * dt, t.p1.y + t.vel.y * dt, t.p1.z + t.vel.z * dt};
        }
    }

    size_t Size() const { return tracks_.size(); }

private:
    struct Track
    {
        EntityState e;
        Vec3 p1, vel;
        double t1 = 0;
        bool hasVel = false, used = false;
    };
    static double Dist(const Vec3& a, const Vec3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); }

    std::vector<Track> tracks_, next_;
    uint64_t seq_ = 0;
    bool have_ = false;
};
