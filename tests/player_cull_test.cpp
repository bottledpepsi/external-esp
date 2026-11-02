// player_cull_test - far players. Reproduces the old "drop the box when it is under 2 px wide or tall" rule, shows that it removed players that
// were still several pixels tall, and checks that ProjectBox with a minimum size keeps them.
#include "Camera.h"
#include "ScreenBox.h"
#include <cmath>
#include <cstdio>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

// what DrawPlayer did before this change (the box without any minimum, then the cull)
static bool OldRuleDrops(const ScreenBox& b) { return (b.x1 - b.x0) < 2.f || (b.y1 - b.y0) < 2.f; }

static Camera Cam(int w, int h)
{
    Camera c;
    c.eye = {0, 65.62, 0};
    c.yawDeg = 0; c.pitchDeg = 0; c.fovYDeg = 70; c.w = w; c.h = h;
    c.Update();
    return c;
}
// a player standing `d` blocks straight ahead (yaw 0 looks toward +z)
static Vec3 Ahead(double d) { return {0, 64, d}; }

int main()
{
    const double pw = 0.6, ph = 1.8; // the default hitbox

    // ---- the problem, reproduced: 1080p, 70 deg ----
    {
        const Camera cam = Cam(1920, 1080);
        ScreenBox b;
        CHECK(ProjectBox(cam, Ahead(100), pw, ph, 0.f, b));
        CHECK(!OldRuleDrops(b) && (b.y1 - b.y0) > 12.f);          // 100 blocks: ~14 px tall, drawn either way
        double firstDrop = 0, heightThere = 0;
        for (double d = 50; d < 1000; d += 1)
        {
            ScreenBox t;
            if (!ProjectBox(cam, Ahead(d), pw, ph, 0.f, t)) continue;
            if (OldRuleDrops(t)) { firstDrop = d; heightThere = t.y1 - t.y0; break; }
        }
        std::printf("old rule first drops a player at %.0f blocks, where the box is still %.1f px tall\n", firstDrop, heightThere);
        CHECK(firstDrop > 200 && firstDrop < 260);
        CHECK(heightThere > 5.f);                                    // 5+ px tall: clearly visible, and gone
        // the width is the limiting side: 0.6/1.8 = a third of the height
        ScreenBox t;
        CHECK(ProjectBox(cam, Ahead(firstDrop), pw, ph, 0.f, t));
        CHECK((t.x1 - t.x0) < 2.f && (t.y1 - t.y0) > 2.9f * (t.x1 - t.x0));
    }

    // ---- the fix: with the default minimum (2 px) every player in front of the camera is kept, up to any distance ----
    for (int res = 0; res < 3; ++res)
    {
        const int w = res == 0 ? 1280 : res == 1 ? 1920 : 3840, h = res == 0 ? 720 : res == 1 ? 1080 : 2160;
        const Camera cam = Cam(w, h);
        for (double d = 5; d <= 5000; d *= 1.15)
        {
            ScreenBox b, raw;
            CHECK(ProjectBox(cam, Ahead(d), pw, ph, 2.f, b));
            CHECK((b.x1 - b.x0) >= 2.f - 1e-3f && (b.y1 - b.y0) >= 2.f - 1e-3f);
            // growth is around the centre: the box stays where the player is
            CHECK(ProjectBox(cam, Ahead(d), pw, ph, 0.f, raw));
            CHECK(std::fabs((b.x0 + b.x1) - (raw.x0 + raw.x1)) < 1e-2f && std::fabs((b.y0 + b.y1) - (raw.y0 + raw.y1)) < 1e-2f);
            // a box that was already big enough is untouched
            if (!OldRuleDrops(raw)) CHECK(std::fabs(b.x0 - raw.x0) < 1e-3f && std::fabs(b.y1 - raw.y1) < 1e-3f && std::fabs(b.x1 - raw.x1) < 1e-3f);
        }
    }

    // ---- everything that was visible before is drawn exactly as before (default 2 px); only previously dropped boxes are new ----
    {
        const Camera cam = Cam(1920, 1080);
        int same = 0, added = 0;
        for (double d = 5; d < 1000; d += 0.5)
        {
            ScreenBox raw, b;
            if (!ProjectBox(cam, Ahead(d), pw, ph, 0.f, raw)) continue;
            CHECK(ProjectBox(cam, Ahead(d), pw, ph, 2.f, b));
            if (OldRuleDrops(raw)) ++added;
            else { ++same; CHECK(b.x0 == raw.x0 && b.x1 == raw.x1 && b.y0 == raw.y0 && b.y1 == raw.y1); }
        }
        CHECK(same > 400 && added > 400);
    }

    // ---- PlayerMinPx = 0 means true size ----
    {
        const Camera cam = Cam(1920, 1080);
        ScreenBox b;
        CHECK(ProjectBox(cam, Ahead(2000), pw, ph, 0.f, b));
        CHECK((b.x1 - b.x0) < 1.f);
    }

    // ---- still not drawn: behind the camera, and entirely off-screen ----
    {
        const Camera cam = Cam(1920, 1080);
        ScreenBox b;
        CHECK(!ProjectBox(cam, {0, 64, -20}, pw, ph, 2.f, b));
        CHECK(!ProjectBox(cam, {400, 64, 10}, pw, ph, 2.f, b));
    }

    if (fails) std::printf("%d failure(s)\n", fails); else std::puts("player_cull_test: all passed");
    return fails ? 1 : 0;
}
