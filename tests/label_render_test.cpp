// label_render_test - EspRenderer's label code on a recording Gfx (tests/fake/Gfx.h): fade near the label distance, and names/distances
// that do not sit on top of each other when the boxes are close together on the screen.
#include "EspRenderer.h"
#include "FriendList.h"
#include "Presets.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

namespace
{
    Camera MakeCam()
    {
        Camera c;
        c.eye = {0, 1.62, 0};
        c.yawDeg = 0; // looking along +z
        c.pitchDeg = 0;
        c.fovYDeg = 70;
        c.w = 1920;
        c.h = 1080;
        c.Update();
        return c;
    }

    PlayerState Player(const std::string& name, double x, double z)
    {
        PlayerState p;
        p.pos = {x, 0, z};
        p.name = name;
        p.sizeValid = true;
        return p;
    }

    labels::Rect RectOf(const TextCall& t, float h)
    {
        const float w = 6.f * static_cast<float>(t.text.size());
        return {t.x - w * 0.5f, t.y, t.x + w * 0.5f, t.y + h};
    }

    const TextCall* Find(const Gfx& g, const std::string& s)
    {
        for (const TextCall& t : g.texts) if (t.text == s) return &t;
        return nullptr;
    }

    // every drawn text as a rectangle (the fake Gfx measures 6 px per character; heights are the renderer's own)
    std::vector<labels::Rect> AllRects(const Gfx& g)
    {
        std::vector<labels::Rect> r;
        for (const TextCall& t : g.texts) r.push_back(RectOf(t, t.font == Font::UiSmall ? 13.f : 15.f));
        return r;
    }

    int Overlaps(const std::vector<labels::Rect>& r)
    {
        int n = 0;
        for (size_t i = 0; i < r.size(); ++i)
            for (size_t j = i + 1; j < r.size(); ++j) n += labels::Overlap(r[i], r[j]) ? 1 : 0;
        return n;
    }

    Settings Defaults()
    {
        Settings s;
        s.showTracers = false;
        s.showItems = false;
        s.boxStyleIdx = 1; // Sharp
        return s;
    }
} // namespace

int main()
{
    const Camera cam = MakeCam();

    // ---- fade ----
    {
        Settings s = Defaults();
        s.labelDist = 100.f;
        s.labelFadePct = 30;
        WorldSnapshot w;
        w.players = {Player("Near", 0, 20), Player("Edge", 3, 90), Player("Gone", -3, 105)};
        s.maxPlayerDistIdx = 3; // unlimited players
        Gfx g;
        EspRenderer r;
        r.DrawWorld(g, s, cam, w);
        const TextCall* near = Find(g, "Near");
        const TextCall* edge = Find(g, "Edge");
        CHECK(near && std::fabs(near->col.a - 1.f) < 1e-4f); // inside 70 m: fully opaque
        CHECK(edge && std::fabs(edge->col.a - (1.f - 20.f / 30.f)) < 0.01f); // 90 m: a third left
        CHECK(Find(g, "Gone") == nullptr); // beyond the label distance
        CHECK(Find(g, "20m") && Find(g, "90m") && !Find(g, "105m")); // distance texts follow the same rule
        const TextCall* d90 = Find(g, "90m");
        CHECK(d90 && d90->col.a < 0.9f * 0.5f); // faded too (base alpha 0.9)

        s.labelFadePct = 0; // the old hard cut
        Gfx g0;
        r.DrawWorld(g0, s, cam, w);
        const TextCall* edge0 = Find(g0, "Edge");
        CHECK(edge0 && std::fabs(edge0->col.a - 1.f) < 1e-4f && Find(g0, "Gone") == nullptr);

        s.labelDist = presets::kUnlimited; // unlimited: never faded
        Gfx gu;
        r.DrawWorld(gu, s, cam, w);
        CHECK(Find(gu, "Gone") && std::fabs(Find(gu, "Gone")->col.a - 1.f) < 1e-4f);
    }

    // ---- declutter ----
    {
        // five players at about the same depth and a few pixels apart: their names land on one line
        WorldSnapshot w;
        w.players = {Player("Alice", 0.0, 20), Player("Bob", 0.25, 20.1), Player("Carol", -0.25, 20.2),
                     Player("Dave", 0.5, 20.3), Player("Eve", -0.5, 20.4)};
        Settings s = Defaults();
        s.maxPlayerDistIdx = 3;
        s.showHealth = false;

        s.labelDeclutter = false;
        Gfx off;
        EspRenderer r;
        r.DrawWorld(off, s, cam, w);
        CHECK(off.texts.size() == 10); // 5 names + 5 distances
        CHECK(Overlaps(AllRects(off)) > 0); // the scene is meaningful: without declutter they collide (the old behaviour)

        s.labelDeclutter = true;
        Gfx on;
        r.DrawWorld(on, s, cam, w);
        CHECK(Overlaps(AllRects(on)) == 0);
        CHECK(on.texts.size() + static_cast<size_t>(r.LabelsDropped()) <= 10);
        // the nearest player's name stays where it was; others move up, never down into the box
        const TextCall* aOn = Find(on, "Alice");
        const TextCall* aOff = Find(off, "Alice");
        CHECK(aOn && aOff && std::fabs(aOn->y - aOff->y) < 1e-3f);
        for (const char* n : {"Bob", "Carol", "Dave", "Eve"})
        {
            const TextCall* a = Find(on, n);
            const TextCall* b = Find(off, n);
            if (a && b) CHECK(a->y <= b->y + 1e-3f);
        }

        // nothing carries over from a frame to the next
        Gfx again;
        r.DrawWorld(again, s, cam, w);
        CHECK(again.texts.size() == on.texts.size());
        for (size_t i = 0; i < on.texts.size(); ++i) CHECK(std::fabs(again.texts[i].y - on.texts[i].y) < 1e-3f);

        // a lone player is untouched by declutter
        WorldSnapshot one;
        one.players = {Player("Solo", 0, 30)};
        Gfx a, b;
        s.labelDeclutter = true;
        r.DrawWorld(a, s, cam, one);
        s.labelDeclutter = false;
        r.DrawWorld(b, s, cam, one);
        CHECK(a.texts.size() == 2 && b.texts.size() == 2);
        for (size_t i = 0; i < 2; ++i) CHECK(std::fabs(a.texts[i].y - b.texts[i].y) < 1e-3f);
    }

    // ---- item labels give way to player labels ----
    {
        Settings s = Defaults();
        s.showItems = true;
        s.maxPlayerDistIdx = 3;
        WorldSnapshot w;
        w.players = {Player("Alice", 0, 12)};
        ItemState it;
        it.pos = {0, 0, 12};
        it.key = "minecraft:diamond";
        it.count = 3;
        w.items = {it, it};
        Gfx g;
        EspRenderer r;
        r.DrawWorld(g, s, cam, w);
        CHECK(Overlaps(AllRects(g)) == 0);
        CHECK(Find(g, "Alice") != nullptr);
    }

    // ---- performance panel and the jitter probe ----
    {
        Settings s = Defaults();
        s.maxPlayerDistIdx = 3;
        EspRenderer r;
        double x = 0, y = 0;
        WorldSnapshot w;
        CHECK(!r.ProbeNearestPlayer(s, cam, w, x, y)); // nobody
        w.players = {Player("Far", 4, 40), Player("Near", 0, 12), Player("Behind", 0, -5), Player("TooClose", 0, 0.5)};
        CHECK(r.ProbeNearestPlayer(s, cam, w, x, y) && std::fabs(x - 960.0) < 1.0 && y > 400 && y < 700); // the near one, in front, centred
        w.players = {Player("Behind", 0, -5)};
        CHECK(!r.ProbeNearestPlayer(s, cam, w, x, y));
        w.players = {Player("Side", 40, 12)}; // far off to the side: outside the viewport + 10 %
        CHECK(!r.ProbeNearestPlayer(s, cam, w, x, y));
        s.maxPlayerDistIdx = 0; // 50 m
        w.players = {Player("Far", 0, 80)};
        CHECK(!r.ProbeNearestPlayer(s, cam, w, x, y));

        perf::PerfHistory h;
        for (int i = 0; i < 240; ++i)
        {
            h.frameMs.Push(8.f + static_cast<float>(i % 5));
            h.ageMs.Push(6.f);
        }
        h.frameText = "frame 9.9 ms";
        h.ageText = "data age 6.0 ms";
        h.jitterText = "jitter: no player in view";
        Gfx g;
        r.DrawPerfPanel(g, 1080, h);
        CHECK(g.lines >= 2 * 239 && g.lines <= 2 * 239 + 4); // one segment per step per graph, and the half-way lines
        CHECK(Find(g, "frame 9.9 ms") && Find(g, "data age 6.0 ms") && Find(g, "jitter: no player in view") && Find(g, "16 ms") && Find(g, "8 ms"));
        for (const TextCall& t : g.texts) CHECK(t.y > 700 && t.y < 1080); // bottom-left, on the screen
        perf::PerfHistory empty;
        Gfx g2;
        r.DrawPerfPanel(g2, 1080, empty);
        CHECK(g2.lines == 2 && g2.texts.size() == 2); // empty history: the frames, no curve, no captions (the two "N ms" tops)
    }

    // ---- the raw "ghost" outlines ----
    {
        Settings s = Defaults();
        s.showItems = true;
        s.maxPlayerDistIdx = 0; // 50 m
        WorldSnapshot raw;
        raw.players = {Player("Near", 0, 15), Player("Far", 3, 80)};
        ItemState it;
        it.pos = {1, 0, 12};
        it.key = "minecraft:diamond";
        raw.items = {it};
        EspRenderer r;
        Gfx g;
        r.DrawGhosts(g, s, cam, raw);
        CHECK(g.rects == 2 && g.texts.empty() && g.lines == 0); // the near player and the item; the far player is past 50 m; never any text
        s.showItems = false;
        Gfx g2;
        r.DrawGhosts(g2, s, cam, raw);
        CHECK(g2.rects == 1);
        s.showPlayers = false;
        Gfx g3;
        r.DrawGhosts(g3, s, cam, raw);
        CHECK(g3.rects == 0);
        // a player behind the camera is not drawn
        s.showPlayers = true;
        WorldSnapshot behind;
        behind.players = {Player("Behind", 0, -20)};
        Gfx g4;
        r.DrawGhosts(g4, s, cam, behind);
        CHECK(g4.rects == 0);
    }

    // ---- item filters: what is drawn and what is counted as hidden ----
    {
        Settings s = Defaults();
        s.showItems = true;
        WorldSnapshot w;
        auto mk = [](const char* key, double x)
        {
            ItemState it;
            it.pos = {x, 0, 15};
            it.key = key;
            return it;
        };
        w.items = {mk("minecraft:diamond", -1), mk("minecraft:oak_planks", 0), mk("minecraft:rotten_flesh", 1), mk("minecraft:gold_ingot", 2)};
        EspRenderer r;
        {
            Gfx g;
            r.DrawWorld(g, s, cam, w);
            CHECK(r.ItemsShown() == 4 && r.ItemsFiltered() == 0); // defaults: everything
        }
        s.hideJunk = true;
        {
            Gfx g;
            r.DrawWorld(g, s, cam, w);
            CHECK(r.ItemsShown() == 3 && r.ItemsFiltered() == 1);
        }
        s.hideJunk = false;
        s.itemMinTier = 2; // valuable and rare only
        {
            Gfx g;
            r.DrawWorld(g, s, cam, w);
            CHECK(r.ItemsShown() == 2 && r.ItemsFiltered() == 2 && Find(g, "Diamond") && !Find(g, "Oak Planks"));
        }
        s.itemMinTier = 0;
        s.itemFilterMode = 1; // whitelist
        s.itemFilterList = "diamond*";
        {
            Gfx g;
            r.DrawWorld(g, s, cam, w);
            CHECK(r.ItemsShown() == 1 && r.ItemsFiltered() == 3 && Find(g, "Diamond") && !Find(g, "Gold Ingot"));
        }
        s.itemFilterList = "diamond*, gold ingot"; // the list is parsed again when its text changes
        {
            Gfx g;
            r.DrawWorld(g, s, cam, w);
            CHECK(r.ItemsShown() == 2);
        }
        s.itemFilterMode = 2; // blacklist
        {
            Gfx g;
            r.DrawWorld(g, s, cam, w);
            CHECK(r.ItemsShown() == 2 && !Find(g, "Diamond") && Find(g, "Oak Planks"));
        }
        s.showItems = false;
        {
            Gfx g;
            r.DrawWorld(g, s, cam, w);
            CHECK(r.ItemsShown() == 0 && r.ItemsFiltered() == 0 && g.texts.empty()); // counters are per frame
        }
    }

    if (fails) std::printf("%d failure(s)\n", fails);
    else std::printf("label_render_test: ok\n");
    return fails ? 1 : 0;
}
