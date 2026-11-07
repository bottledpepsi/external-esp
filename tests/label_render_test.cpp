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

    // ---- the theme's colors are what gets drawn ----
    {
        Settings s = Defaults();
        s.maxPlayerDistIdx = 3;
        s.showItems = true;
        s.itemOpacityIdx = static_cast<int>(presets::TraceOpacities.size()) - 1; // the most opaque preset
        s.colors.text = Rgba{0.1f, 0.2f, 0.3f, 1.f};
        s.colors.distance = Rgba{0.4f, 0.5f, 0.6f, 1.f};
        s.colors.item = Rgba{0.7f, 0.8f, 0.9f, 1.f};
        s.colors.itemEnchanted = Rgba{0.9f, 0.1f, 0.5f, 1.f};
        s.colors.death = Rgba{0.2f, 0.9f, 0.2f, 1.f};
        WorldSnapshot w;
        w.players = {Player("Alice", 0, 20)};
        ItemState plain, ench;
        plain.pos = {-3, 0, 20};
        plain.key = "minecraft:stick";
        ench.pos = {3, 0, 20};
        ench.key = "minecraft:iron_sword";
        ench.enchants.push_back({"minecraft:sharpness", 3});
        w.items = {plain, ench};
        EspRenderer r;
        Gfx g;
        r.DrawWorld(g, s, cam, w);
        const TextCall* name = Find(g, "Alice");
        const TextCall* dist = Find(g, "20m");
        CHECK(name && std::fabs(name->col.r - 0.1f) < 1e-4f && std::fabs(name->col.g - 0.2f) < 1e-4f && std::fabs(name->col.b - 0.3f) < 1e-4f);
        CHECK(dist && std::fabs(dist->col.r - 0.4f) < 1e-4f && std::fabs(dist->col.b - 0.6f) < 1e-4f);
        bool gotPlain = false, gotEnch = false;
        for (const Col& c : g.rectCols)
        {
            gotPlain = gotPlain || (std::fabs(c.r - 0.7f) < 1e-4f && std::fabs(c.g - 0.8f) < 1e-4f && std::fabs(c.b - 0.9f) < 1e-4f);
            gotEnch = gotEnch || (std::fabs(c.r - 0.9f) < 1e-4f && std::fabs(c.g - 0.1f) < 1e-4f && std::fabs(c.b - 0.5f) < 1e-4f);
        }
        CHECK(gotPlain && gotEnch); // the item boxes carry the item and enchanted colors
        Gfx d;
        r.DrawDeathMarker(d, s, cam, {0, 0, 30}, 1920, 1080);
        CHECK(d.segs.size() == 4 && std::fabs(d.segs[0].col.r - 0.2f) < 1e-4f && std::fabs(d.segs[0].col.g - 0.9f) < 1e-4f);
        // the default theme draws what was drawn before themes: a name in pure white, the distance in the old pale blue
        Settings def = Defaults();
        def.maxPlayerDistIdx = 3;
        Gfx g0;
        r.DrawWorld(g0, def, cam, w);
        CHECK(Find(g0, "Alice") && Find(g0, "Alice")->col.r == 1.f && Find(g0, "Alice")->col.g == 1.f && std::fabs(Find(g0, "20m")->col.r - 0.85f) < 1e-6f &&
              std::fabs(Find(g0, "20m")->col.a - 0.9f) < 1e-6f);
    }

    // ---- tracers: origin, distance cap, players and mob categories ----
    {
        Settings s = Defaults();
        s.showTracers = true;
        s.maxPlayerDistIdx = 3;
        s.showHealth = false;
        s.tracerFade = false; // (the distance fade would hide the 150 m one by itself)
        EspRenderer r;
        WorldSnapshot w;
        w.players = {Player("Near", 2, 20), Player("Mid", -2, 60), Player("Far", 0, 150)};
        // the three players, unlimited tracer distance: three lines, all starting at the bottom centre
        {
            Gfx g;
            r.DrawWorld(g, s, cam, w);
            CHECK(g.segs.size() == 3);
            for (const auto& l : g.segs) CHECK(std::fabs(l.x0 - 960.f) < 1e-3f && std::fabs(l.y0 - 1080.f) < 1e-3f);
        }
        s.tracerOriginIdx = 1;
        {
            Gfx g;
            r.DrawWorld(g, s, cam, w);
            CHECK(g.segs.size() == 3 && std::fabs(g.segs[0].y0 - 540.f) < 1e-3f);
        }
        s.tracerOriginIdx = 2;
        {
            Gfx g;
            r.DrawWorld(g, s, cam, w);
            CHECK(g.segs.size() == 3 && g.segs[0].y0 == 0.f && std::fabs(g.segs[0].x0 - 960.f) < 1e-3f);
        }
        s.tracerOriginIdx = 0;
        s.tracerMaxIdx = 1; // 50 m
        {
            Gfx g;
            r.DrawWorld(g, s, cam, w);
            CHECK(g.segs.size() == 1); // only the one at 20 m
        }
        s.tracerMaxIdx = 2; // 100 m
        {
            Gfx g;
            r.DrawWorld(g, s, cam, w);
            CHECK(g.segs.size() == 2);
        }
        s.tracerMaxIdx = 3;
        s.tracerPlayers = false;
        {
            Gfx g;
            r.DrawWorld(g, s, cam, w);
            CHECK(g.segs.empty()); // players' tracers off: boxes and labels remain
            CHECK(Find(g, "Near") != nullptr);
        }
        s.tracerPlayers = true;
        s.showTracers = false;
        {
            Gfx g;
            r.DrawWorld(g, s, cam, w);
            CHECK(g.segs.empty()); // the master switch
        }

        // mobs: only the categories with their tracer on, in the category's color, within the distance, and only while the mob is shown
        s.showTracers = true;
        s.tracerPlayers = false;
        s.showEntities = true;
        std::vector<EntityState> es(4);
        const EntityKind kinds[] = {EntityKind::Hostile, EntityKind::Passive, EntityKind::Hostile, EntityKind::Projectile};
        for (int i = 0; i < 4; ++i)
        {
            es[static_cast<size_t>(i)].id = static_cast<uint32_t>(i + 1);
            es[static_cast<size_t>(i)].kind = kinds[i];
            es[static_cast<size_t>(i)].pos = {static_cast<double>(i) - 1.5, 0, 15.0 + i};
            es[static_cast<size_t>(i)].label = "M";
        }
        es[2].pos.z = 90; // a hostile 90 m away: beyond the default 64 m entity distance, so not shown at all
        {
            Gfx g;
            r.DrawEntities(g, s, cam, es);
            CHECK(g.segs.size() == 1); // default: hostile only; the far one is not shown; the passive and the projectile have no tracer
            CHECK(std::fabs(g.segs[0].col.r - presets::EntityPaletteRgb[0].r) < 1e-4f); // red = hostile
        }
        s.tracerEntity[static_cast<int>(EntityKind::Passive)] = true;
        s.tracerEntity[static_cast<int>(EntityKind::Projectile)] = true;
        {
            Gfx g;
            r.DrawEntities(g, s, cam, es);
            CHECK(g.segs.size() == 3);
        }
        s.tracerMaxIdx = 0; // 25 m
        {
            Gfx g;
            r.DrawEntities(g, s, cam, es);
            CHECK(g.segs.size() == 3); // all three are within 25 m
        }
        es[0].pos.z = 28;
        {
            Gfx g;
            r.DrawEntities(g, s, cam, es);
            CHECK(g.segs.size() == 2); // one of them is now beyond the tracer distance (still shown, no line)
            CHECK(r.EntitiesShown() == 3);
        }
        s.tracerMaxIdx = 3;
        s.entShow[static_cast<int>(EntityKind::Passive)] = false; // not shown: no box and no tracer
        {
            Gfx g;
            r.DrawEntities(g, s, cam, es);
            CHECK(g.segs.size() == 2 && r.EntitiesShown() == 2);
        }
        s.showEntities = false;
        {
            Gfx g;
            r.DrawEntities(g, s, cam, es);
            CHECK(g.segs.empty());
        }
        // behind the camera: no line, no crash
        s.showEntities = true;
        es[0].pos.z = -10;
        {
            Gfx g;
            r.DrawEntities(g, s, cam, std::vector<EntityState>{es[0]});
            CHECK(g.segs.empty());
        }
    }

    // ---- the radar: corner, orientation, entities, death marker ----
    {
        const int W = 1920, H = 1080;
        const float mm = static_cast<float>(presets::MinimapSizes[1]);
        WorldSnapshot w;
        w.localValid = true;
        w.local.pos = {0, 0, 0};
        EspRenderer r;
        auto panel = [&](const Settings& s, const WorldSnapshot& snap, const EspRenderer::MinimapExtras& x)
        {
            Gfx g;
            r.DrawMinimap(g, s, cam, snap, W, H, x);
            return g;
        };
        // corners
        Settings s = Defaults();
        for (int c = 0; c < 4; ++c)
        {
            s.minimapCorner = c;
            const Gfx g = panel(s, w, {});
            CHECK(g.panels.size() == 1);
            const float ex = (c == 0 || c == 2) ? W - mm - 12.f : 12.f, ey = (c == 2 || c == 3) ? H - mm - 12.f : 12.f;
            CHECK(std::fabs(g.panels[0].x - ex) < 1e-3f && std::fabs(g.panels[0].y - ey) < 1e-3f && std::fabs(g.panels[0].w - mm) < 1e-3f);
        }
        s.minimapCorner = 0;
        // north-up (the cam looks along +z): a player north (-z) is above the centre, one east (+x) to the right, one south below
        w.players = {Player("North", 0, -20), Player("East", 20, 0), Player("South", 0, 20)};
        s.maxPlayerDistIdx = 3;
        {
            const Gfx g = panel(s, w, {});
            const float cx = g.panels[0].x + mm * 0.5f, cy = g.panels[0].y + mm * 0.5f;
            // the first three discs are the players, then the self dot
            CHECK(g.discs.size() == 4);
            CHECK(std::fabs(g.discs[0].x - cx) < 1e-3f && g.discs[0].y < cy);
            CHECK(g.discs[1].x > cx && std::fabs(g.discs[1].y - cy) < 1e-3f);
            CHECK(std::fabs(g.discs[2].x - cx) < 1e-3f && g.discs[2].y > cy);
        }
        // rotate: facing +z (yaw 0) is up, so the +z player is above, the -z one below; your right is west (-x)
        s.minimapMode = 1;
        {
            const Gfx g = panel(s, w, {});
            const float cx = g.panels[0].x + mm * 0.5f, cy = g.panels[0].y + mm * 0.5f;
            CHECK(g.discs[0].y > cy && std::fabs(g.discs[0].x - cx) < 1e-3f); // "North" (-z) is behind you
            CHECK(g.discs[1].x < cx); // "East" (+x) is on your left when you face +z
            CHECK(g.discs[2].y < cy); // "South" (+z) is ahead
            bool n = false;
            for (const TextCall& t : g.texts) n = n || t.text == "N";
            CHECK(n); // and the map says where north went
        }
        // turn the camera to face +x (yaw -90: forward = (-sin yaw, cos yaw) = (1, 0)): the east player is ahead, so above
        {
            Camera east = cam;
            east.yawDeg = -90;
            east.Update();
            Gfx g;
            r.DrawMinimap(g, s, east, w, W, H, {});
            const float cx = g.panels[0].x + mm * 0.5f, cy = g.panels[0].y + mm * 0.5f;
            CHECK(g.discs[1].y < cy && std::fabs(g.discs[1].x - cx) < 1e-2f);
        }
        s.minimapMode = 0;
        // the rim: a player very far away is pinned to the rim, never outside the panel
        w.players = {Player("Far", 5000, -5000)};
        {
            const Gfx g = panel(s, w, {});
            const float cx = g.panels[0].x + mm * 0.5f, cy = g.panels[0].y + mm * 0.5f;
            CHECK(std::hypot(g.discs[0].x - cx, g.discs[0].y - cy) <= mm * 0.5f - 3.9f);
        }
        w.players.clear();
        // entities: small dots in their category color, only the shown categories and only inside the rim
        {
            std::vector<EntityState> es(3);
            es[0].kind = EntityKind::Hostile;
            es[0].pos = {0, 0, -20};
            es[1].kind = EntityKind::Passive;
            es[1].pos = {10, 0, 0};
            es[2].kind = EntityKind::Hostile;
            es[2].pos = {0, 0, -5000}; // outside the radar: left out
            EspRenderer::MinimapExtras x;
            x.entities = &es;
            Settings t = s;
            t.showEntities = true;
            Gfx g = panel(t, w, x);
            CHECK(g.discs.size() == 3); // two entities and the self dot
            CHECK(std::fabs(g.discs[0].col.r - presets::EntityPaletteRgb[0].r) < 1e-4f); // hostile: red
            t.entShow[static_cast<int>(EntityKind::Passive)] = false;
            g = panel(t, w, x);
            CHECK(g.discs.size() == 2);
            t.showEntities = false;
            g = panel(t, w, x);
            CHECK(g.discs.size() == 1); // master off: none
            x.entities = nullptr;
            t.showEntities = true;
            CHECK(panel(t, w, x).discs.size() == 1);
        }
        // the death marker: a red cross at the spot, pinned (and dimmer) at the rim when it is far
        {
            EspRenderer::MinimapExtras x;
            x.hasDeath = true;
            x.death = {0, 0, 30};
            const Gfx none = panel(s, w, {});
            const Gfx g = panel(s, w, x);
            CHECK(g.segs.size() == none.segs.size() + 2);
            const auto& a = g.segs[g.segs.size() - 2];
            CHECK(a.col.r == 1.f && a.col.g < 0.5f && a.col.a == 1.f);
            x.death = {0, 0, 9000};
            const Gfx far = panel(s, w, x);
            CHECK(far.segs.back().col.a < 1.f); // pinned: dimmer
        }
    }

    // ---- the death marker in the world ----
    {
        EspRenderer r;
        Settings s = Defaults();
        {
            Gfx g;
            r.DrawDeathMarker(g, s, cam, {0, 0, 87}, 1920, 1080); // straight ahead, 87 m
            CHECK(g.segs.size() == 4 && Find(g, "Death  87m"));
            const TextCall* t = Find(g, "Death  87m");
            CHECK(t && std::fabs(t->x - 960.f) < 2.f && t->y < 540.f + 40.f); // centred, around the horizon
        }
        {
            Gfx g;
            r.DrawDeathMarker(g, s, cam, {0, 0, -40}, 1920, 1080); // behind: bottom centre, not off the screen
            const TextCall* t = Find(g, "Death  40m");
            CHECK(t && std::fabs(t->x - 960.f) < 1.f && t->y > 900.f && t->y < 1080.f);
        }
        {
            Gfx g;
            r.DrawDeathMarker(g, s, cam, {500, 0, 10}, 1920, 1080); // far to the right: clamped to the screen
            const TextCall* t = Find(g, "Death  500m");
            CHECK(t && t->x <= 1920.f - 30.f + 1.f && t->x >= 30.f);
        }
    }

    // ---- entities: categories, distance, colors, labels ----
    {
        auto ent = [](uint32_t id, EntityKind k, double x, double z, const char* label, float hp = 20.f)
        {
            EntityState e;
            e.id = id;
            e.kind = k;
            e.pos = {x, 0, z};
            e.label = label;
            e.sizeValid = true;
            e.width = 0.6f;
            e.height = 1.8f;
            e.healthValid = k != EntityKind::Projectile && k != EntityKind::Xp;
            e.health = hp;
            return e;
        };
        std::vector<EntityState> list = {ent(1, EntityKind::Hostile, 0, 12, "Zombie", 10.f), ent(2, EntityKind::Passive, 2, 14, "Cow"),
                                         ent(3, EntityKind::Projectile, -2, 16, "Arrow"), ent(4, EntityKind::Xp, 3, 18, "ExperienceOrb"),
                                         ent(5, EntityKind::Other, -3, 20, "Blob"), ent(6, EntityKind::Neutral, 4, 22, "Wolf"),
                                         ent(7, EntityKind::Hostile, 0, 90, "Skeleton")};
        Settings s = Defaults();
        s.showHealth = true;
        EspRenderer r;
        {
            Gfx g;
            r.DrawEntities(g, s, cam, list); // master switch off: nothing drawn, but the list is counted
            CHECK(g.rects == 0 && g.texts.empty() && r.EntitiesShown() == 0 && r.EntitiesSeen() == 7);
        }
        s.showEntities = true; // defaults: every category but "other", within 64 m
        {
            Gfx g;
            r.DrawEntities(g, s, cam, list);
            CHECK(r.EntitiesShown() == 5 && r.EntitiesSeen() == 7); // Other hidden, Skeleton at 90 m too far
            CHECK(r.EntityKindShown(static_cast<int>(EntityKind::Hostile)) == 1 && r.EntityKindShown(static_cast<int>(EntityKind::Other)) == 0 &&
                  r.EntityKindShown(static_cast<int>(EntityKind::Projectile)) == 1 && r.EntityKindShown(99) == 0);
            CHECK(Find(g, "Zombie") && Find(g, "Cow") && Find(g, "Wolf")); // mobs have names
            CHECK(!Find(g, "Arrow") && !Find(g, "ExperienceOrb") && !Find(g, "Blob") && !Find(g, "Skeleton")); // projectiles / orbs have none; the others are hidden / far
            CHECK(Find(g, "12m") && Find(g, "14m") && Find(g, "22m") && !Find(g, "16m")); // distances under the names
            CHECK(Overlaps(AllRects(g)) == 0);
            const TextCall* z = Find(g, "Zombie");
            const presets::Rgb red = presets::EntityPaletteRgb[0]; // Hostile defaults to Red
            CHECK(z && std::fabs(z->col.r - red.r) < 1e-4f && std::fabs(z->col.g - red.g) < 1e-4f);
            const TextCall* c = Find(g, "Cow");
            const presets::Rgb green = presets::EntityPaletteRgb[3];
            CHECK(c && std::fabs(c->col.g - green.g) < 1e-4f);
        }
        s.entShow[static_cast<int>(EntityKind::Other)] = true;
        s.entShow[static_cast<int>(EntityKind::Hostile)] = false;
        s.entDistIdx = 3; // unlimited
        {
            Gfx g;
            r.DrawEntities(g, s, cam, list);
            CHECK(r.EntitiesShown() == 5 && r.EntityKindShown(static_cast<int>(EntityKind::Hostile)) == 0 && r.EntityKindShown(static_cast<int>(EntityKind::Other)) == 1);
            CHECK(!Find(g, "Zombie") && !Find(g, "Skeleton") && Find(g, "Blob"));
        }
        s.entLabels = false;
        {
            Gfx g;
            r.DrawEntities(g, s, cam, list);
            CHECK(g.texts.empty() && r.EntitiesShown() == 5); // boxes only
        }
        s.entLabels = true;
        s.entShow[static_cast<int>(EntityKind::Hostile)] = true;
        s.entColor[static_cast<int>(EntityKind::Hostile)] = 5; // Blue
        s.entColor[static_cast<int>(EntityKind::Passive)] = 99; // garbage index: clamped, not a crash
        {
            Gfx g;
            r.DrawEntities(g, s, cam, list);
            const TextCall* z = Find(g, "Zombie");
            CHECK(z && std::fabs(z->col.b - presets::EntityPaletteRgb[5].b) < 1e-4f && Find(g, "Cow"));
        }
        // health bars: a living mob's box has a bar (two extra rectangles), a projectile's has none
        {
            Settings t = Defaults();
            t.showEntities = true;
            t.entLabels = false;
            t.showHealth = true;
            std::vector<EntityState> one = {ent(1, EntityKind::Hostile, 0, 12, "Zombie", 10.f)}, arrow = {ent(2, EntityKind::Projectile, 0, 12, "Arrow")};
            Gfx a, b, c;
            r.DrawEntities(a, t, cam, one);
            t.showHealth = false;
            r.DrawEntities(b, t, cam, one);
            r.DrawEntities(c, t, cam, arrow);
            CHECK(a.rects == b.rects + 2 && c.rects == 1);
        }
        // an entity behind the camera, nothing in the list, and many entities (the label queue is bounded)
        {
            Settings t = Defaults();
            t.showEntities = true;
            std::vector<EntityState> none, behind = {ent(1, EntityKind::Hostile, 0, -10, "Zombie")}, crowd;
            Gfx a, b, c;
            r.DrawEntities(a, t, cam, none);
            r.DrawEntities(b, t, cam, behind);
            CHECK(a.rects == 0 && b.rects == 0 && r.EntitiesShown() == 0 && r.EntitiesSeen() == 1);
            for (uint32_t i = 0; i < 500; ++i) crowd.push_back(ent(i, EntityKind::Passive, (i % 20) * 0.3 - 3, 10 + (i % 7), "Pig"));
            r.DrawEntities(c, t, cam, crowd);
            CHECK(r.EntitiesSeen() == 500 && r.EntitiesShown() > 0 && c.texts.size() <= 128 * 2);
        }
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
