// entity_tracker_test - EntityTracker: velocity from two samples of one id, the extrapolation cap, teleports, id churn; and the Smoother's list numbers.
#include "EntityTracker.h"
#include "Smoother.h"
#include "Wire.h"
#include <cmath>
#include <cstdio>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
static bool Near(double a, double b, double e = 1e-6) { return std::fabs(a - b) < e; }

static EntityState Ent(uint32_t id, double x, double y, double z, EntityKind k = EntityKind::Hostile)
{
    EntityState e;
    e.id = id;
    e.kind = k;
    e.pos = {x, y, z};
    e.label = "Zombie";
    return e;
}

int main()
{
    EntityTracker t;
    std::vector<EntityState> out;

    // ---- the first sample: no velocity, so no extrapolation ----
    t.Update(1, {Ent(7, 10, 64, 20)}, 100.0);
    t.Predict(100.05, out);
    CHECK(out.size() == 1 && Near(out[0].pos.x, 10) && out[0].label == "Zombie" && out[0].id == 7);

    // ---- the second one gives a velocity: 2 blocks in 0.1 s = 20 blocks/s along x, -10 along z ----
    t.Update(2, {Ent(7, 12, 64, 19)}, 100.1);
    t.Predict(100.1, out);
    CHECK(Near(out[0].pos.x, 12) && Near(out[0].pos.z, 19)); // at the sample time: the sample
    t.Predict(100.15, out);
    CHECK(Near(out[0].pos.x, 13) && Near(out[0].pos.z, 18.5) && Near(out[0].pos.y, 64)); // 50 ms on
    t.Predict(100.1 + 1.0, out);
    CHECK(Near(out[0].pos.x, 12 + 20 * EntityTracker::kMaxExtrapolation)); // never further than the cap
    t.Predict(99.0, out);
    CHECK(Near(out[0].pos.x, 12)); // a time before the sample: the sample

    // ---- the same list number again: ignored ----
    t.Update(2, {Ent(7, 99, 64, 99)}, 100.2);
    t.Predict(100.1, out);
    CHECK(Near(out[0].pos.x, 12));

    // ---- teleport / reused id: no velocity ----
    t.Update(3, {Ent(7, 40, 64, 19)}, 100.2); // 28 blocks in 0.1 s
    t.Predict(100.3, out);
    CHECK(Near(out[0].pos.x, 40));
    // ... and the next sample after it does have one again
    t.Update(4, {Ent(7, 41, 64, 19)}, 100.3);
    t.Predict(100.35, out);
    CHECK(Near(out[0].pos.x, 41.5));

    // ---- a long gap between samples: no velocity ----
    t.Update(5, {Ent(7, 41.5, 64, 19)}, 105.0);
    t.Predict(105.1, out);
    CHECK(Near(out[0].pos.x, 41.5));
    // two samples too close together: no velocity (dt would be about zero)
    t.Update(6, {Ent(7, 42, 64, 19)}, 105.001);
    t.Predict(105.1, out);
    CHECK(Near(out[0].pos.x, 42));

    // ---- a different kind with the same id is a different entity ----
    t.Update(7, {Ent(7, 43, 64, 19, EntityKind::Passive)}, 105.1);
    t.Predict(105.2, out);
    CHECK(out.size() == 1 && out[0].kind == EntityKind::Passive && Near(out[0].pos.x, 43));

    // ---- entities come and go; two with the same id in one list each follow their own track ----
    t.Clear();
    t.Update(1, {Ent(1, 0, 64, 0), Ent(2, 10, 64, 0), Ent(2, 20, 64, 0), Ent(3, 5, 64, 5)}, 10.0);
    t.Update(2, {Ent(2, 11, 64, 0), Ent(2, 21, 64, 0), Ent(9, 5, 64, 5)}, 10.1);
    t.Predict(10.15, out);
    CHECK(out.size() == 3 && t.Size() == 3);
    CHECK(Near(out[0].pos.x, 11 + 10 * 0.05) && Near(out[1].pos.x, 21 + 10 * 0.05) && Near(out[2].pos.x, 5)); // 1 and 3 are gone, 9 is new
    t.Update(3, {}, 10.2);
    t.Predict(10.2, out);
    CHECK(out.empty() && t.Size() == 0);

    // ---- no allocation after warm-up (capacity is reused) ----
    {
        EntityTracker w;
        std::vector<EntityState> list, o;
        for (int i = 0; i < 100; ++i) list.push_back(Ent(static_cast<uint32_t>(i), i, 64, 0));
        w.Update(1, list, 1.0);
        w.Predict(1.0, o);
        const auto cap = o.capacity();
        for (int k = 2; k < 200; ++k)
        {
            for (auto& e : list) e.pos.x += 0.1;
            w.Update(static_cast<uint64_t>(k), list, 1.0 + k * 0.1);
            w.Predict(1.0 + k * 0.1 + 0.05, o);
        }
        CHECK(o.size() == 100 && o.capacity() == cap && Near(o[50].pos.x, 50 + 0.1 * 198 + 0.05 * 1.0, 1e-6)); // x += 0.1 per 0.1 s = 1 block/s
    }

    // ---- the Smoother numbers each list and keeps the number while it keeps the list ----
    {
        Smoother sm;
        auto snap = [](double ts, bool lists)
        {
            WorldSnapshot s;
            const std::string d = std::string("MCRADAR 6\nT ") + std::to_string(static_cast<long long>(5e9 + ts * 1e9)) + "\nS ok\n" +
                (lists ? "F 1\nE 4 1 1 2 3 0.6 1.8 20 Zombie\n" : "");
            ParseDatagram(d.data(), d.size(), s);
            s.seq = static_cast<uint64_t>(ts * 1000) + 1;
            s.arrived = Smoother::Clock::now() + std::chrono::milliseconds(static_cast<int>(ts * 1000));
            return s;
        };
        sm.Push(snap(0.0, true));
        const uint64_t a = sm.Newest().listSeq;
        const auto arrivedA = sm.ListArrived();
        CHECK(a != 0 && sm.Newest().entities.size() == 1);
        sm.Push(snap(0.02, false));
        sm.Push(snap(0.04, false));
        CHECK(sm.Newest().listSeq == a && sm.Newest().entities.size() == 1 && sm.ListArrived() == arrivedA); // the old list, the old number, the old time
        sm.Push(snap(0.1, true));
        CHECK(sm.Newest().listSeq == a + 1 && sm.ListArrived() > arrivedA);
        sm.Push(snap(0.12, false));
        CHECK(sm.Newest().listSeq == a + 1);
        // the tracker fed from it takes each list once
        EntityTracker tr;
        std::vector<EntityState> o;
        tr.Update(sm.Newest().listSeq, sm.Newest().entities, 1.0);
        tr.Update(sm.Newest().listSeq, sm.Newest().entities, 1.05); // the same list: not a second sample
        tr.Predict(1.05, o);
        CHECK(o.size() == 1 && Near(o[0].pos.x, 1.0));
    }

    if (fails) std::printf("%d failure(s)\n", fails);
    else std::printf("entity_tracker_test: ok\n");
    return fails ? 1 : 0;
}
