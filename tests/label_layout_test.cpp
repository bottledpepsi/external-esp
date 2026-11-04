// label_layout_test - LabelLayout.h: the fade curve and the greedy placer.
#include "LabelLayout.h"
#include <cmath>
#include <cstdio>
using namespace labels;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
static bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

int main()
{
    // ---- Fade ----
    CHECK(Near(Fade(0, 100, 30), 1.f) && Near(Fade(70, 100, 30), 1.f)); // full up to 70 %
    CHECK(Near(Fade(85, 100, 30), 0.5f)); // half way down the last 30 m
    CHECK(Near(Fade(100, 100, 30), 0.f) && Fade(100.01f, 100, 30) == 0.f);
    CHECK(Near(Fade(100, 100, 0), 1.f) && Fade(100.01f, 100, 0) == 0.f); // 0 % = the old hard cut
    CHECK(Near(Fade(60, 100, 100), 0.4f)); // 100 %: fades from the camera outwards
    CHECK(Near(Fade(1e6f, 3.4028235e38f, 30), 1.f)); // unlimited never fades
    CHECK(Near(Fade(50, 100, -5), 1.f) && Near(Fade(85, 100, 400), Fade(85, 100, 100))); // out-of-range percentages are clamped
    CHECK(Near(Fade(NAN, 100, 30), 1.f) && Near(Fade(-3, 100, 30), 1.f)); // garbage distance: draw it
    for (float d = 0; d <= 100; d += 0.5f) CHECK(Fade(d, 100, 30) >= Fade(d + 0.5f, 100, 30)); // never rises with distance

    // ---- Overlap ----
    CHECK(Overlap({0, 0, 10, 10}, {5, 5, 15, 15}));
    CHECK(!Overlap({0, 0, 10, 10}, {10, 0, 20, 10})); // touching edges are not an overlap
    CHECK(!Overlap({0, 0, 10, 10}, {0, 10, 10, 20}));

    // ---- Placer ----
    {
        Placer p;
        Rect a{0, 100, 40, 114};
        CHECK(p.Place(a, -1, 14, 3) && a.y0 == 100 && p.Count() == 1); // free: stays
        Rect b{10, 100, 50, 114};
        CHECK(p.Place(b, -1, 14, 3) && Near(b.y0, 86) && Near(b.y1, 100) && Near(b.Height(), 14)); // moved up one step, same size
        Rect c{20, 100, 60, 114};
        CHECK(p.Place(c, -1, 14, 3) && Near(c.y0, 72)); // and the next one above that
        Rect d{20, 100, 60, 114};
        CHECK(p.Place(d, -1, 14, 3) && Near(d.y0, 58)); // 3 steps is the limit
        Rect e{20, 100, 60, 114};
        CHECK(!p.Place(e, -1, 14, 3) && e.y0 == 100 && p.Dropped() == 1 && p.Count() == 4); // no room: dropped, rect untouched
        Rect f{200, 100, 240, 114};
        CHECK(p.Place(f, -1, 14, 3) && f.y0 == 100); // somewhere else on the screen: free
        p.Reset();
        CHECK(p.Count() == 0 && p.Dropped() == 0);
        Rect g{0, 100, 40, 114};
        CHECK(p.Place(g, -1, 14, 3) && g.y0 == 100); // Reset forgot everything
    }
    {
        Placer p; // downward
        Rect a{0, 0, 40, 13}, b{0, 0, 40, 13};
        CHECK(p.Place(a, +1, 13, 2) && p.Place(b, +1, 13, 2) && Near(b.y0, 13));
    }
    {
        Placer p; // Occupy takes space without testing
        p.Occupy({0, 0, 40, 14});
        Rect a{0, 0, 40, 14};
        CHECK(p.Place(a, -1, 14, 2) && Near(a.y0, -14));
    }
    {
        // the capacity: the first kCapacity labels are kept, later ones are dropped (and counted), and nothing is written out of bounds
        Placer p;
        int placed = 0;
        for (int i = 0; i < Placer::kCapacity + 10; ++i)
        {
            Rect r{static_cast<float>(i) * 50.f, 0, static_cast<float>(i) * 50.f + 40.f, 14};
            if (p.Place(r, -1, 14, 0)) ++placed;
        }
        CHECK(placed == Placer::kCapacity && p.Dropped() == 10);
    }

    if (fails) std::printf("%d failure(s)\n", fails);
    else std::printf("label_layout_test: ok\n");
    return fails ? 1 : 0;
}
