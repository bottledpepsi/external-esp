// death_tracker_test - DeathTracker: dying, the death screen, respawning, and when the marker goes away.
#include "DeathTracker.h"
#include <cstdio>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

static void Alive(DeathTracker& d, double t, double x, double z) { d.Update(t, true, 20.f, true, {x, 64, z}); }
static void Zero(DeathTracker& d, double t, double x, double z) { d.Update(t, true, 0.f, true, {x, 64, z}); }

int main()
{
    // ---- a normal life ----
    {
        DeathTracker d;
        for (int i = 0; i < 100; ++i) Alive(d, i * 0.05, i, 0);
        CHECK(!d.HasMarker() && !d.Dead() && d.Deaths() == 0);
        d.Update(6, true, 3.5f, true, {1, 64, 1}); // hurt is not dead
        CHECK(!d.HasMarker() && !d.Dead());
    }

    // ---- death: needs 0.25 s of zero health; the marker is where the first zero was ----
    {
        DeathTracker d;
        Alive(d, 0.0, 10, 20);
        Zero(d, 0.1, 11, 21); // the first dead sample
        CHECK(!d.Dead() && !d.HasMarker()); // not yet
        Zero(d, 0.2, 11.5, 21.5);
        CHECK(!d.Dead());
        Zero(d, 0.36, 12, 22); // 0.26 s after the first
        CHECK(d.Dead() && d.HasMarker() && d.Deaths() == 1 && d.DiedAt() == 0.1);
        CHECK(d.Marker().x == 11 && d.Marker().z == 21); // not where the body slid to
        Zero(d, 5.0, 50, 50); // the death screen goes on: still one death, the marker does not move
        CHECK(d.Dead() && d.Deaths() == 1 && d.Marker().x == 11);
    }
    // ---- a single zero while loading is not a death ----
    {
        DeathTracker d;
        Alive(d, 0.0, 0, 0);
        Zero(d, 0.1, 0, 0);
        Alive(d, 0.15, 0, 0); // back up before the 0.25 s
        Zero(d, 0.5, 0, 0);
        Alive(d, 0.6, 0, 0);
        CHECK(!d.Dead() && !d.HasMarker() && d.Deaths() == 0);
        // and the confirmation timer restarts: two short zeros do not add up
        Zero(d, 1.0, 0, 0);
        Alive(d, 1.1, 0, 0);
        Zero(d, 1.2, 0, 0);
        Zero(d, 1.3, 0, 0);
        CHECK(!d.Dead());
    }

    // ---- respawn: the marker stays; getting there clears it ----
    {
        DeathTracker d;
        Alive(d, 0, 100, 100);
        Zero(d, 1.0, 100, 100);
        Zero(d, 1.5, 100, 100);
        CHECK(d.Dead());
        Alive(d, 10.0, 0, 0); // respawned at the spawn point, 140 blocks away
        CHECK(!d.Dead() && d.HasMarker() && d.Marker().x == 100);
        Alive(d, 12.0, 50, 50);
        CHECK(d.HasMarker());
        Alive(d, 14.0, 98.5, 100.5); // within 3 blocks
        CHECK(!d.HasMarker() && d.Deaths() == 1);
    }
    // ---- respawning right next to the death place (a bed): not cleared in the grace second, cleared a moment later ----
    {
        DeathTracker d;
        Alive(d, 0, 5, 5);
        Zero(d, 1.0, 5, 5);
        Zero(d, 1.4, 5, 5);
        Alive(d, 5.0, 5.5, 5.5);
        CHECK(d.HasMarker());
        Alive(d, 5.5, 5.5, 5.5);
        CHECK(d.HasMarker());
        Alive(d, 6.2, 5.5, 5.5);
        CHECK(!d.HasMarker());
    }
    // ---- a second death replaces the marker ----
    {
        DeathTracker d;
        Alive(d, 0, 0, 0);
        Zero(d, 1, 10, 0);
        Zero(d, 1.5, 10, 0);
        Alive(d, 3, -40, -40);
        Zero(d, 8, -45, -45);
        Zero(d, 8.5, -45, -45);
        CHECK(d.Deaths() == 2 && d.Marker().x == -45 && d.Dead());
    }
    // ---- expiry ----
    {
        DeathTracker d;
        Alive(d, 0, 0, 0);
        Zero(d, 1, 10, 0);
        Zero(d, 1.5, 10, 0);
        Alive(d, 3, 300, 300);
        Alive(d, 500, 300, 300);
        CHECK(d.HasMarker());
        Alive(d, 700, 300, 300);
        CHECK(!d.HasMarker());
    }
    // ---- no health reading: nothing is concluded, nothing is lost ----
    {
        DeathTracker d;
        d.Update(0, false, 0, true, {0, 0, 0});
        d.Update(5, false, 0, true, {0, 0, 0});
        CHECK(!d.Dead() && !d.HasMarker());
        Alive(d, 6, 1, 1);
        Zero(d, 7, 1, 1);
        Zero(d, 7.5, 1, 1);
        CHECK(d.Dead());
        d.Update(9, false, 0, false, {}); // the reading drops out during the death screen
        CHECK(d.Dead() && d.HasMarker());
    }
    // ---- a teleport is not a death ----
    {
        DeathTracker d;
        Alive(d, 0, 0, 0);
        Alive(d, 0.05, 5000, 5000);
        CHECK(!d.Dead() && !d.HasMarker());
    }
    // ---- no position: uses the last good one; with none at all, no marker (it could not be drawn) ----
    {
        DeathTracker d;
        Alive(d, 0, 7, 8);
        d.Update(1.0, true, 0.f, false, {});
        d.Update(1.5, true, 0.f, false, {});
        CHECK(d.Dead() && d.Marker().x == 7 && d.Marker().z == 8);
        DeathTracker e;
        e.Update(1.0, true, 0.f, false, {});
        e.Update(1.5, true, 0.f, false, {});
        CHECK(!e.Dead() && !e.HasMarker());
    }
    // ---- Clear / Reset ----
    {
        DeathTracker d;
        Alive(d, 0, 1, 1);
        Zero(d, 1, 1, 1);
        Zero(d, 1.5, 1, 1);
        d.Clear();
        CHECK(!d.HasMarker() && d.Dead() && d.Deaths() == 1); // clearing the marker is not clearing the fact that the player is dead
        d.Reset();
        CHECK(!d.Dead() && d.Deaths() == 0);
    }

    if (fails) std::printf("%d failure(s)\n", fails);
    else std::printf("death_tracker_test: ok\n");
    return fails ? 1 : 0;
}
