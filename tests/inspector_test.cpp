// inspector_test - the entity inspector's overlay half: X lines, the control file text, picking the entity at the crosshair, and the panel.
#include "ControlFile.h"
#include "EspRenderer.h"
#include "Smoother.h"
#include "Wire.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

static WorldSnapshot P(const std::string& body, int version = 6)
{
    WorldSnapshot s;
    const std::string d = "MCRADAR " + std::to_string(version) + "\n" + body;
    ParseDatagram(d.data(), d.size(), s);
    return s;
}
static const TextCall* Find(const Gfx& g, const std::string& s)
{
    for (const TextCall& t : g.texts) if (t.text == s) return &t;
    return nullptr;
}

int main()
{
    // ---- the encoder's own datagram (the committed corpus file) ----
    {
        std::ifstream f(std::string(ESP_TEST_DATA_DIR) + "/wire/inspect_v6.dgram", std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        WorldSnapshot s;
        CHECK(!ss.str().empty() && ParseDatagram(ss.str().data(), ss.str().size(), s) && s.ignoredLines == 0);
        CHECK(s.inspect.valid && s.inspect.id == 1234567 && s.inspect.rows.size() == 4 && s.entities.size() == 3);
        CHECK(s.inspect.rows[0].key == "class" && s.inspect.rows[0].value == "Zombie > Monster > PathfinderMob > Mob > LivingEntity > Entity");
        CHECK(s.inspect.rows[1].key == "position path" && s.inspect.rows[1].value == "Entity.position -> x,y,z");
        CHECK(s.inspect.rows[2].key == "Entity.tickCount : int" && s.inspect.rows[2].value == "1200");
        CHECK(s.inspect.rows[3].value == "\"Evil Name\\nX 9 k v\""); // the text survives, and a quoted "\n" is just characters
    }

    // ---- X lines: validation ----
    {
        CHECK(!P("S x\n").inspect.valid);
        const WorldSnapshot ok = P("X 5 key value\nX 5 other%20key some%20value\nX 5 e -\nX 5 dash %2D\n");
        CHECK(ok.inspect.valid && ok.inspect.id == 5 && ok.inspect.rows.size() == 4 && ok.inspect.rows[1].key == "other key" && ok.inspect.rows[1].value == "some value" && ok.ignoredLines == 0);
        CHECK(ok.inspect.rows[2].value.empty() && ok.inspect.rows[3].value == "-"); // a bare "-" is "nothing" on the wire, as everywhere; an escaped one is a dash
        CHECK(P("X 0 wire v6", 6).inspect.id == 0 && P("X 0 wire v6").inspect.valid); // id 0 = the agent's own paths
        // older versions ignore them
        for (int v : {4, 5}) CHECK(!P("X 5 k v\n", v).inspect.valid);
        const char* bad[] = {"X 5 k\n", "X 5\n", "X -1 k v\n", "X 4294967296 k v\n", "X abc k v\n", "X 5.5 k v\n", "X 1e3 k v\n"};
        for (const char* b : bad)
        {
            const WorldSnapshot s = P(std::string(b) + "S fine\n");
            CHECK(!s.inspect.valid && s.ignoredLines == 1 && s.status == "fine");
        }
        // every row of a datagram is about the first one's id
        const WorldSnapshot mix = P("X 5 a 1\nX 6 b 2\nX 5 c 3\n");
        CHECK(mix.inspect.id == 5 && mix.inspect.rows.size() == 2 && mix.ignoredLines == 1);
        // text: decoded, cleaned, limited
        const WorldSnapshot t = P("X 5 k%00ey%0A " + std::string(600, 'v') + "\nX 5 " + std::string(300, 'k') + " v%FF\n");
        CHECK(t.inspect.rows.size() == 2);
        CHECK(t.inspect.rows[0].key.find('\0') == std::string::npos && t.inspect.rows[0].key.find('\n') == std::string::npos && t.inspect.rows[0].value.size() == wire::kMaxInspectValue);
        CHECK(t.inspect.rows[1].key.size() == wire::kMaxInspectKey);
        std::string many;
        for (int i = 0; i < 100; ++i) many += "X 5 k" + std::to_string(i) + " v\n";
        const WorldSnapshot m = P(many);
        CHECK(m.inspect.rows.size() == wire::kMaxInspectRows && m.ignoredLines == 100 - wire::kMaxInspectRows && m.inspect.rows[0].key == "k0");
    }

    // ---- the Smoother keeps the answer with the lists, and drops it when a list arrives without one ----
    {
        Smoother sm;
        auto snap = [](double ts, const std::string& lists)
        {
            WorldSnapshot s = P(std::string("T ") + std::to_string(static_cast<long long>(5e9 + ts * 1e9)) + "\nS ok\n" + lists);
            s.seq = static_cast<uint64_t>(ts * 1000) + 1;
            s.arrived = Smoother::Clock::now();
            return s;
        };
        sm.Push(snap(0.0, "F 1\nX 7 class Zombie\nX 7 id 7\n"));
        CHECK(sm.Newest().inspect.valid && sm.Newest().inspect.rows.size() == 2);
        sm.Push(snap(0.02, "")); // no list this time: the answer stays, like the lists
        CHECK(sm.Newest().inspect.valid && sm.Newest().inspect.id == 7 && sm.Newest().inspect.rows.size() == 2);
        sm.Push(snap(0.10, "F 1\nX 7 class Zombie\n"));
        CHECK(sm.Newest().inspect.rows.size() == 1);
        sm.Push(snap(0.20, "F 1\n")); // the overlay stopped inspecting: a list without X rows clears it
        CHECK(!sm.Newest().inspect.valid && sm.Newest().inspect.rows.empty());
    }

    // ---- the control file ----
    {
        using namespace controlfile;
        CHECK(RequestEntity(1234567) == "inspect 1234567\n" && RequestEntity(4294967295u) == "inspect 4294967295\n" && RequestEntity(0) == "inspect 0\n");
        CHECK(RequestAgent() == "inspect player\n" && RequestOff() == "inspect off\n");
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / "esp ctl test";
        CHECK(AgentOption(dir / kFileName) == "ctl=" + (dir / kFileName).string());
        CHECK(AgentOption("relative/agent.ctl").empty() && AgentOption("").empty());
        CHECK(AgentOption(dir / "a;b.ctl").empty() && AgentOption(dir / "a=b.ctl").empty() && AgentOption(dir / "a\nb.ctl").empty()); // the option string is split at ; and =
        CHECK(AgentOption(std::filesystem::path("/" + std::string(300, 'x'))).empty());
        // Write: makes the folder, replaces the file, touches it every time
        std::filesystem::remove_all(dir);
        const auto file = dir / "sub" / kFileName;
        CHECK(Write(file, RequestEntity(5)));
        CHECK(std::filesystem::exists(file));
        const auto t1 = std::filesystem::last_write_time(file);
        std::filesystem::last_write_time(file, t1 - std::chrono::seconds(30)); // pretend it was written a while ago
        CHECK(Write(file, RequestEntity(5))); // the same text again is a new write: this is the keep-alive
        CHECK(std::filesystem::last_write_time(file) > t1 - std::chrono::seconds(30));
        {
            std::ifstream f(file);
            std::string line;
            std::getline(f, line);
            CHECK(line == "inspect 5");
        } // closed: Windows cannot delete a file that is still open
        std::error_code ec;
        std::filesystem::remove_all(dir, ec); // a leftover temp folder is not a failure
    }

    // ---- picking the entity at the crosshair ----
    Camera cam;
    cam.eye = {0, 1.62, 0};
    cam.yawDeg = 0;
    cam.pitchDeg = 0;
    cam.fovYDeg = 70;
    cam.w = 1920;
    cam.h = 1080;
    cam.Update();
    auto ent = [](uint32_t id, EntityKind k, double x, double z)
    {
        EntityState e;
        e.id = id;
        e.kind = k;
        e.pos = {x, 0, z};
        e.sizeValid = true;
        e.width = 0.6f;
        e.height = 1.8f;
        e.label = "Mob" + std::to_string(id);
        return e;
    };
    Settings s;
    s.showEntities = true;
    EspRenderer r;
    {
        std::vector<EntityState> l = {ent(1, EntityKind::Hostile, 6, 20), ent(2, EntityKind::Hostile, 0.3, 30), ent(3, EntityKind::Passive, -8, 15)};
        const EntityState* p = r.PickNearestCrosshair(s, cam, l);
        CHECK(p && p->id == 2); // 0.3 m to the side at 30 m is nearest to the middle of the screen
        l.push_back(ent(4, EntityKind::Hostile, 0.0, 12)); // straight ahead but close: its middle is lower on the screen (46 px) than the one at 30 m (20 px)
        CHECK(r.PickNearestCrosshair(s, cam, l)->id == 2);
        l.push_back(ent(10, EntityKind::Hostile, 0.05, 40)); // far and almost exactly in line: its middle is 14 px from the crosshair
        CHECK(r.PickNearestCrosshair(s, cam, l)->id == 10);
        CHECK(!r.PickNearestCrosshair(s, cam, {}));
        CHECK(!r.PickNearestCrosshair(s, cam, {ent(5, EntityKind::Hostile, 40, 20)}, 220.f)); // too far from the crosshair in pixels
        CHECK(r.PickNearestCrosshair(s, cam, {ent(5, EntityKind::Hostile, 40, 20)}, 2000.f)->id == 5); // ... unless the limit is wide
        CHECK(!r.PickNearestCrosshair(s, cam, {ent(6, EntityKind::Hostile, 0, -10)})); // behind the camera
        CHECK(!r.PickNearestCrosshair(s, cam, {ent(7, EntityKind::Hostile, 0, 100)})); // beyond the default 64 m
        Settings t = s;
        t.entShow[static_cast<int>(EntityKind::Hostile)] = false; // a hidden category cannot be picked
        CHECK(!r.PickNearestCrosshair(t, cam, {ent(8, EntityKind::Hostile, 0, 12)}));
        CHECK(r.PickNearestCrosshair(t, cam, {ent(8, EntityKind::Hostile, 0, 12), ent(9, EntityKind::Passive, 1, 12)})->id == 9);
    }

    // ---- the panel ----
    {
        std::vector<InspectRow> rows = {{"class", "Zombie > Monster"}, {"position", "1.000 2.000 3.000"}, {std::string(80, 'k'), std::string(200, 'v')}};
        EspRenderer::InspectView v;
        v.title = "Inspect: Zombie  #5";
        v.rows = &rows;
        const EntityState e = ent(5, EntityKind::Hostile, 0, 20);
        v.entity = &e;
        Gfx g;
        r.DrawInspector(g, s, cam, 1920, 1080, v);
        CHECK(Find(g, "Inspect: Zombie  #5") && Find(g, "class") && Find(g, "Zombie > Monster") && Find(g, "1.000 2.000 3.000"));
        CHECK(g.panels.size() == 1 && g.panels[0].x > 1920 - 420 - 20 && g.panels[0].x + g.panels[0].w <= 1920 && g.panels[0].y + g.panels[0].h <= 1080);
        CHECK(g.rectCols.size() >= 1 && g.rectCols.back().r == 1.f && g.rectCols.back().a > 0.9f); // the white frame round the entity (the first Rect)
        for (const TextCall& t : g.texts) CHECK(t.x >= g.panels[0].x && t.x < 1920 && t.y >= g.panels[0].y && t.y < 1080);
        bool clipped = false;
        for (const TextCall& t : g.texts) clipped = clipped || (t.text.size() <= 38 && t.text.back() == '~');
        CHECK(clipped); // long keys and values are cut with a ~
        // not answered yet: the note, no rows
        EspRenderer::InspectView w;
        w.title = "Inspect: how the agent found things";
        w.note = "waiting for the agent";
        Gfx g2;
        r.DrawInspector(g2, s, cam, 1920, 1080, w);
        CHECK(Find(g2, "waiting for the agent") && g2.panels.size() == 1 && g2.rectCols.empty());
        // very many rows: at most 34 are drawn, and the panel stays on the screen even in a small window
        std::vector<InspectRow> many(100, InspectRow{"k", "v"});
        EspRenderer::InspectView m;
        m.title = "t";
        m.rows = &many;
        Gfx g3;
        r.DrawInspector(g3, s, cam, 1280, 720, m);
        int ks = 0;
        for (const TextCall& t : g3.texts) ks += t.text == "k" ? 1 : 0;
        CHECK(ks == 34 && g3.panels[0].y + g3.panels[0].h <= 720.f + 1.f);
        // an entity behind the camera is not framed, and no crash
        const EntityState behind = ent(6, EntityKind::Hostile, 0, -20);
        EspRenderer::InspectView b;
        b.title = "t";
        b.note = "n";
        b.entity = &behind;
        Gfx g4;
        r.DrawInspector(g4, s, cam, 1920, 1080, b);
        CHECK(g4.rectCols.empty());
    }

    if (fails) std::printf("%d failure(s)\n", fails);
    else std::printf("inspector_test: ok\n");
    return fails ? 1 : 0;
}
