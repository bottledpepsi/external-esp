// Wire parser: (1) the REAL Java encoder's output parses to exactly what it encoded (cross-language contract, tests/data/wire/*.dgram),
// (2) a corpus of hostile / malformed datagrams, (3) a seeded mutation fuzzer. The parser must never crash, throw or hang on any input; with
// ASan/UBSan (see the sanitizer jobs) "never crash" includes memory errors and undefined behaviour. This file asserts only what must hold
// for ANY input; the stricter value rules (finite numbers, limits) are in wire_validation_test.cpp.
//   ESP_FUZZ_ITERS=N   number of mutation iterations (default 60000)
#include "Wire.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifndef ESP_TEST_DATA_DIR
#error "ESP_TEST_DATA_DIR must point at tests/data"
#endif

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
static bool near(double a, double b, double e = 1e-4) { return std::fabs(a - b) <= e; }

static std::string ReadFile(const std::string& name)
{
    std::ifstream f(std::string(ESP_TEST_DATA_DIR) + "/wire/" + name, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static double g_worstMs = 0;
static bool Parse(const std::string& d, WorldSnapshot& out)
{
    const auto a = std::chrono::steady_clock::now();
    bool ok = false;
    try { ok = ParseDatagram(d.data(), d.size(), out); }
    catch (...) { std::printf("FAIL: ParseDatagram threw on a %zu-byte datagram\n", d.size()); ++fails; }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
    if (ms > g_worstMs) g_worstMs = ms;
    return ok;
}

struct Rng
{
    uint64_t s;
    uint64_t Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    size_t Below(size_t n) { return n ? static_cast<size_t>(Next() % n) : 0; }
};

int main()
{
    // ================= (1) the Java encoder's real output =================
    {
        const std::string full = ReadFile("full_v4.dgram");
        CHECK(!full.empty());
        WorldSnapshot s;
        CHECK(Parse(full, s));
        CHECK(s.status == "agent ok: 1 other player(s)" && s.hasSampleTime && s.sampleNs == 123456789012345LL && s.hasItems);
        CHECK(s.camera.valid && near(s.camera.pos.x, 97.9276) && near(s.camera.pos.y, 65.62) && near(s.camera.pos.z, -45.4497));
        CHECK(near(s.camera.yawDeg, -175.83, 1e-3) && near(s.camera.pitchDeg, 18.91, 1e-3) && s.camera.fovValid && near(s.camera.fovDeg, 68.02, 1e-3) && s.camera.mode == 2);
        CHECK(s.localValid && s.local.name == "Alice" && near(s.local.pos.x, 100) && near(s.local.pos.z, -50) && s.local.yawValid && near(s.local.yawDeg, 12.5, 1e-3));
        CHECK(s.local.pitchValid && near(s.local.pitchDeg, -3.25, 1e-3) && s.local.healthValid && near(s.local.health, 17.5) && s.local.sizeValid);
        CHECK(near(s.local.width, 0.6, 1e-3) && near(s.local.height, 1.8, 1e-3) && s.local.uuid == "069a79f4-44e9-4726-a5be-fca90e38aaf5");
        CHECK(s.players.size() == 1 && s.players[0].name == "Bob" && near(s.players[0].pos.y, 70) && near(s.players[0].pos.z, -60.25));
        CHECK(s.items.size() == 2);
        if (s.items.size() == 2)
        {
            CHECK(s.items[0].key == "minecraft:diamond_sword" && s.items[0].count == 1 && s.items[0].sizeValid && near(s.items[0].width, 0.25, 1e-3));
            CHECK(s.items[0].enchants.size() == 2 && s.items[0].enchants[0].id == "minecraft:sharpness" && s.items[0].enchants[0].level == 5);
            CHECK(s.items[0].enchants[1].id == "minecraft:unbreaking" && s.items[0].enchants[1].level == 3);
            CHECK(s.items[0].tags == "{Enchantments:[{id:\"minecraft:sharpness\"}]}");
            CHECK(s.items[1].key == "minecraft:cobblestone" && s.items[1].count == 37 && !s.items[1].sizeValid && s.items[1].enchants.empty() && s.items[1].tags.empty());
        }

        WorldSnapshot b;
        CHECK(Parse(ReadFile("bare_v4.dgram"), b) && !b.hasItems && !b.localValid && b.players.empty() && !b.camera.valid && !b.hasSampleTime);

        WorldSnapshot u;
        CHECK(Parse(ReadFile("unicode_v4.dgram"), u) && u.players.size() == 3);
        if (u.players.size() == 3)
            CHECK(u.players[0].name == "J\xC3\xBCrgen" && u.players[1].name == "\xD0\x96\xD0\xB5\xD0\xBD\xD1\x8F" && u.players[2].name == "\xF0\x9F\x98\x80");
        CHECK(u.status.find("\xC3\xA9\xC3\xA8") != std::string::npos);

        WorldSnapshot c;
        CHECK(Parse(ReadFile("crowd_v4.dgram"), c) && c.players.size() == 40 && c.items.size() == 60 && c.hasItems);
        if (c.players.size() == 40) CHECK(c.players[39].name == "Player39" && near(c.players[39].pos.x, 97.5));
        if (c.items.size() == 60) CHECK(c.items[59].key == "minecraft:item_59" && c.items[59].count == 1 + 59 % 64);
    }

    // ================= (2) hostile corpus =================
    std::vector<std::string> corpus;
    auto add = [&](std::string s) { corpus.push_back(std::move(s)); };
    add("");
    add("\n");
    add("MCRADAR");
    add("MCRADAR 4");
    add("MCRADAR 4\n");
    add("MCRADAR 4\r\nS ok\r\nL A 1 2 3\r\n");
    add(std::string("MCRADAR 4\nL A 1 2 3\0P B 4 5 6\n", 31));                      // embedded NUL
    add(std::string(70000, 'A'));                                                     // one huge line, no newline
    add("MCRADAR 4\n" + std::string(65000, 'x'));                                     // huge unterminated tag-less line
    add("MCRADAR 4\nS " + std::string(60000, 's') + "\n");                            // huge status
    add("MCRADAR 4\nL " + std::string(60000, 'n') + " 1 2 3\n");                      // huge name
    add("MCRADAR 4\nP a 1 2 3 - - - - - " + std::string(60000, 'u') + "\n");          // huge uuid
    add("MCRADAR 4\nI 1 2 3 - - 1 " + std::string(60000, 'k') + " - -\n");            // huge item key
    add("MCRADAR 4\nI 1 2 3 - - 1 k - " + std::string(60000, 't') + "\n");           // huge tags
    add("MCRADAR 4\nI 1 2 3 - - 1 k " + std::string(30000, '@') + " -\n");           // pathological enchant text
    add("MCRADAR 4\nI 1 2 3 - - 1 k a@1,b@2,,@,@@,a@,@1," + std::string(20000, ',') + " -\n");
    add("MCRADAR 4\nI 1 2 3 - - 1 %zz%%%2%G%0 %%%%%% -\n");                           // broken percent escapes
    add("MCRADAR 4\nI 1 2 3 - - 1 %");                                                // percent at the very end
    {
        std::string many = "MCRADAR 4\n";
        for (int i = 0; i < 6000; ++i) many += "P a" + std::to_string(i) + " 1 2 3\n";  // thousands of players in one datagram
        add(many);
        std::string items = "MCRADAR 4\nF 1\n";
        for (int i = 0; i < 6000; ++i) items += "I 1 2 3 - - 1 k - -\n";
        add(items);
        std::string same = "MCRADAR 4\n";
        for (int i = 0; i < 6000; ++i) same += "P same 1 2 3\n";                          // the same name over and over
        add(same);
        std::string blank = "MCRADAR 4\n" + std::string(60000, '\n');
        add(blank);
        std::string cr = "MCRADAR 4\n" + std::string(30000, '\r') + "\n";
        add(cr);
    }
    const char* numbers[] = {"nan", "NaN", "-nan", "inf", "-inf", "Infinity", "1e999", "-1e999", "1e-999", "0x1p3", "0x", "1e", "e5", ".", "-", "--1", "+1",
                             "1,5", "1.5.5", "99999999999999999999999999999999999999999", "1e38", "3.5e38", "-3.5e38", "0", "-0", "1e30", "00012", " ", "١٢٣"};
    for (const char* n : numbers)
    {
        const std::string v = n;
        add("MCRADAR 4\nL A " + v + " 2 3 " + v + " " + v + " " + v + " " + v + " " + v + " u\nP B 1 " + v + " 3\n");
        add("MCRADAR 4\nC " + v + " 2 3 4 5 " + v + " " + v + "\n");
        add("MCRADAR 4\nC 1 2 3 4 5 70 " + v + "\n");
        add("MCRADAR 4\nI 1 2 3 " + v + " " + v + " " + v + " k a@" + v + " t\n");
        add("MCRADAR 4\nI " + v + " 2 3 - - 1 k - -\n");
        add("MCRADAR 4\nT " + v + "\nS ok\n");
    }
    add("MCRADAR 4\nT 99999999999999999999\n");
    add("MCRADAR 4\nT -99999999999999999999\n");
    add("MCRADAR 4\nT 5 6\nT 7\nT\nT x\n");
    add("MCRADAR 4\nC 1 2 3 4 5 70 99999999999999999999\nC 1 2 3 4 5 0 0\nC 1 2 3 4 5 180 -3\nC 1 2 3 4 5 -5 2\n");
    add("MCRADAR 1\nL A 1 2 3 4\nP B 1 2 3 4\n");
    add("MCRADAR 2\nL A 1 2 3 4 5 6\n");
    add("MCRADAR 3\nC 1 2 3 4 5 70 1\nI 1 2 3 - - 1 k - -\n");
    add("MCRADAR 5\nL A 1 2 3\n");
    add("MCRADAR 10\nL A 1 2 3\n");
    add("MCRADAR 40\nL A 1 2 3\n");
    add("MCRADAR 4junk\nL A 1 2 3\n");
    add("MCRADAR 0\n");
    add("MCRADAR -1\n");
    add("MCRADAR 4\nL \xFF\xFE 1 2 3\nP \xC3\x28 1 2 3\nP \xE2\x82 1 2 3\nP \xF0\x9F\x98 1 2 3\nP \xED\xA0\x80 1 2 3\nP \xC0\xAF 1 2 3\n");   // invalid UTF-8
    add("MCRADAR 4\nS \x01\x02\x1B[31mred\x7F\n");                                    // control characters
    add("MCRADAR 4\nL a\x01\x02 1 2 3\n");
    add("MCRADAR 4\n\n\n  \n x\nS\nS \nL\nL \nL A\nL A 1\nL A 1 2\nP\nP A 1 2\nC\nC 1\nI\nI 1 2\nI 1 2 3\n");   // truncated lines
    add("MCRADAR 4\nZ unknown tag\nl lowercase\n1 2 3\n\t\tL A 1 2 3\n");
    add("MCRADAR 4\nL A\t1\t2\t3\n");                                                  // tabs separate columns too
    for (const std::string& d : corpus)
    {
        WorldSnapshot a, b = a;
        const bool ok = Parse(d, a);
        if (!ok) CHECK(a.players.empty() && !a.localValid && !a.camera.valid && a.items.empty());   // a rejected datagram leaves `out` untouched
        CHECK(a.players.size() <= d.size() / 8 + 1 && a.items.size() <= d.size() / 8 + 1);          // cannot invent more entities than bytes
    }
    std::printf("corpus: %zu hostile datagrams parsed, slowest %.2f ms\n", corpus.size(), g_worstMs);

    // ================= (3) seeded mutation fuzzing =================
    const size_t iters = std::getenv("ESP_FUZZ_ITERS") ? static_cast<size_t>(std::atoll(std::getenv("ESP_FUZZ_ITERS"))) : 60000;
    std::vector<std::string> seeds = {ReadFile("full_v4.dgram"), ReadFile("bare_v4.dgram"), ReadFile("unicode_v4.dgram"), ReadFile("crowd_v4.dgram")};
    for (const std::string& c : corpus) if (c.size() < 4000) seeds.push_back(c);
    Rng rng{0x9E3779B97F4A7C15ULL};
    size_t accepted = 0;
    for (size_t it = 0; it < iters; ++it)
    {
        std::string d = seeds[rng.Below(seeds.size())];
        const size_t muts = 1 + rng.Below(8);
        for (size_t m = 0; m < muts; ++m)
        {
            switch (rng.Below(9))
            {
            case 0: if (!d.empty()) d[rng.Below(d.size())] ^= static_cast<char>(1u << rng.Below(8)); break;                    // bit flip
            case 1: if (!d.empty()) d[rng.Below(d.size())] = static_cast<char>(rng.Next()); break;                              // random byte
            case 2: { std::string ins; for (size_t i = 0, n = 1 + rng.Below(12); i < n; ++i) ins += static_cast<char>(rng.Next());
                      d.insert(rng.Below(d.size() + 1), ins); break; }                                                          // insert bytes
            case 3: if (!d.empty()) { const size_t a = rng.Below(d.size()); d.erase(a, 1 + rng.Below(std::min<size_t>(40, d.size() - a))); } break;   // delete range
            case 4: if (!d.empty()) d.resize(rng.Below(d.size() + 1)); break;                                                   // truncate
            case 5: { const size_t a = rng.Below(d.size() + 1), n = rng.Below(std::min<size_t>(200, d.size() - a + 1));
                      d.insert(rng.Below(d.size() + 1), d.substr(a, n)); break; }                                               // duplicate a chunk
            case 6: { const std::string& o = seeds[rng.Below(seeds.size())]; d = d.substr(0, rng.Below(d.size() + 1)) + o.substr(rng.Below(o.size() + 1)); break; }   // splice
            case 7: { static const char* t[] = {"nan", "inf", "-inf", "1e999", "0x1p9", "99999999999999999999", "%", "%zz", "-", "\n", " ", "\r", "@", ","};
                      d.insert(rng.Below(d.size() + 1), t[rng.Below(14)]); break; }                                              // interesting tokens
            default: { const size_t p = d.find('\n', rng.Below(d.size() + 1)); if (p != std::string::npos) d.insert(p, " " + std::to_string(rng.Next())); break; }   // extra column
            }
        }
        WorldSnapshot s;
        if (Parse(d, s)) ++accepted;
    }
    std::printf("fuzz: %zu mutated datagrams (%zu accepted), slowest single parse %.2f ms\n", iters, accepted, g_worstMs);
    CHECK(g_worstMs < 250.0);                                              // no input makes the parser take a quarter of a second

    std::printf(fails ? "\n%d FAILURES\n" : "\nALL OK\n", fails);
    return fails;
}
