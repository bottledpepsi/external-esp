// wire_entities_test - wire v6 "E" lines: the Java encoder's sample, validation of every column, the limits, older versions, and that the
// smoother keeps the entity list together with the item list.
#include "Smoother.h"
#include "Wire.h"
#include "wire_v6_sample.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

static bool Parse(const std::string& d, WorldSnapshot& s) { return ParseDatagram(d.data(), d.size(), s); }
static WorldSnapshot P(const std::string& body, int version = 6)
{
    WorldSnapshot s;
    const std::string d = "MCRADAR " + std::to_string(version) + "\n" + body;
    Parse(d, s);
    return s;
}

int main()
{
    // ---- the encoder's own sample ----
    {
        WorldSnapshot s;
        const std::string d = kV6Full;
        CHECK(Parse(d, s) && s.ignoredLines == 0 && s.entities.size() == 3 && s.items.size() == 2 && s.players.size() == 1 && s.hasItems);
        const EntityState& z = s.entities[0];
        CHECK(z.id == 1234567 && z.kind == EntityKind::Hostile && z.pos.x == 92.5f && z.pos.y == 64.f && z.pos.z == -47.25f && z.sizeValid);
        CHECK(z.width == 0.6f && z.height == 1.95f && z.healthValid && z.health == 20.f && z.label == "Zombie");
        CHECK(s.entities[1].kind == EntityKind::Passive && s.entities[1].label == "Cow" && s.entities[1].health == 10.f);
        const EntityState& a = s.entities[2];
        CHECK(a.id == 4294967295u && a.kind == EntityKind::Projectile && !a.healthValid && a.label == "Arrow"); // the largest id, and "-" = unknown health
    }
    // the committed corpus file is the same datagram
    {
        std::ifstream f(std::string(ESP_TEST_DATA_DIR) + "/wire/full_v6.dgram", std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        WorldSnapshot s;
        CHECK(f.good() || !ss.str().empty());
        CHECK(Parse(ss.str(), s) && s.entities.size() == 3 && s.entities[2].id == 4294967295u);
    }

    // ---- older versions do not read E lines ----
    {
        for (int v : {4, 5}) CHECK(P("E 5 1 1 2 3 0.6 1.8 20 Zombie\n", v).entities.empty());
        CHECK(P("E 5 1 1 2 3 0.6 1.8 20 Zombie\n", 6).entities.size() == 1);
    }

    // ---- every column is checked; a bad line is dropped and counted, the rest of the datagram survives ----
    {
        const char* bad[] = {
            "E 5 1 1 2 3 0.6 1.8 20\n", // too short
            "E -1 1 1 2 3 0.6 1.8 20 Z\n", // negative id
            "E 4294967296 1 1 2 3 0.6 1.8 20 Z\n", // id too big
            "E x 1 1 2 3 0.6 1.8 20 Z\n",
            "E 5 6 1 2 3 0.6 1.8 20 Z\n", // kind out of range
            "E 5 -1 1 2 3 0.6 1.8 20 Z\n",
            "E 5 1.5 1 2 3 0.6 1.8 20 Z\n",
            "E 5 1 nan 2 3 0.6 1.8 20 Z\n",
            "E 5 1 1 inf 3 0.6 1.8 20 Z\n",
            "E 5 1 1 2 1e9 0.6 1.8 20 Z\n", // beyond the world
        };
        for (const char* b : bad)
        {
            const WorldSnapshot s = P(std::string(b) + "S still here\n");
            CHECK(s.entities.empty() && s.ignoredLines == 1 && s.status == "still here");
        }
        // unusable size / health only make those columns unknown
        const WorldSnapshot s = P("E 5 1 1 2 3 0 -4 -3 Z\nE 6 1 1 2 3 99 1e9 2e5 Z\nE 7 1 1 2 3 - - - -\n");
        CHECK(s.entities.size() == 3 && s.ignoredLines == 0);
        for (const EntityState& e : s.entities) CHECK(!e.sizeValid && !e.healthValid);
        CHECK(s.entities[2].label.empty() && s.entities[0].label == "Z");
        const WorldSnapshot ok = P("E 1 0 0 0 0 0.6 1.8 0 Slime\nE 2 5 0 0 0 0.1 0.1 0 Orb\n");
        CHECK(ok.entities.size() == 2 && ok.entities[0].kind == EntityKind::Other && ok.entities[1].kind == EntityKind::Xp && ok.entities[0].healthValid && ok.entities[0].health == 0.f);
    }

    // ---- text and limits ----
    {
        // percent escapes are decoded and then cleaned: no control characters, no invalid UTF-8, never longer than the label limit
        const WorldSnapshot s = P("E 1 1 0 0 0 - - - Zom%00bie%0A%FF\nE 2 1 0 0 0 - - - " + std::string(500, 'a') + "\nE 3 2 0 0 0 - - - Evil%20Name\n");
        CHECK(s.entities.size() == 3);
        for (const EntityState& e : s.entities)
        {
            CHECK(e.label.size() <= wire::kMaxLabelBytes);
            for (unsigned char c : e.label) CHECK(c >= 0x20 || c >= 0x80);
        }
        CHECK(s.entities[0].label.find('\0') == std::string::npos && s.entities[1].label.size() == wire::kMaxLabelBytes && s.entities[2].label == "Evil Name");
        // at most kMaxEntities per datagram; the rest is counted, the datagram still parses
        std::string many;
        for (int i = 0; i < 700; ++i) many += "E " + std::to_string(i) + " 1 " + std::to_string(i) + " 64 0 0.6 1.8 20 Z\n";
        const WorldSnapshot m = P(many + "S end\n");
        CHECK(m.entities.size() == wire::kMaxEntities && m.ignoredLines == 700 - wire::kMaxEntities && m.status == "end" && m.entities[0].id == 0);
    }

    // ---- the Smoother keeps the entities with the items when a datagram has neither ----
    {
        Smoother sm;
        auto snap = [](double t, bool withLists)
        {
            WorldSnapshot s = P(std::string("T ") + std::to_string(static_cast<long long>(5e9 + t * 1e9)) + "\nS ok\n" + (withLists ? "F 1\nI 1 2 3 0.25 0.25 1 minecraft:stick - -\nE 9 1 4 5 6 0.6 1.8 20 Zombie\n" : ""));
            s.seq = static_cast<uint64_t>(t * 100 + 1);
            s.arrived = Smoother::Clock::now();
            return s;
        };
        sm.Push(snap(0.0, true));
        CHECK(sm.Newest().entities.size() == 1 && sm.Newest().items.size() == 1);
        sm.Push(snap(0.01, false)); // no F line: the lists stay
        CHECK(sm.Newest().entities.size() == 1 && sm.Newest().items.size() == 1 && sm.Newest().entities[0].label == "Zombie");
        sm.Push(snap(0.02, true)); // F line with a list: replaced
        CHECK(sm.Newest().entities.size() == 1);
        WorldSnapshot empty = P("T 5030000000\nS ok\nF 1\n"); // F line and nothing after it: the lists are now empty
        empty.seq = 99;
        empty.arrived = Smoother::Clock::now();
        sm.Push(empty);
        CHECK(sm.Newest().entities.empty() && sm.Newest().items.empty());
    }

    if (fails) std::printf("%d failure(s)\n", fails);
    else std::printf("wire_entities_test: ok\n");
    return fails ? 1 : 0;
}
