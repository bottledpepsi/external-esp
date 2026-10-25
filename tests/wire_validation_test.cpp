// Value rules of the wire parser: plain finite numbers in sane ranges, capped counts and strings, valid UTF-8, exact version header.
#include "Wire.h"
#include "wire_invariants.h"
#include <cmath>
#include <cstdio>
#include <string>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

static bool Parse(const std::string& d, WorldSnapshot& s)
{
    const bool ok = ParseDatagram(d.data(), d.size(), s);
    if (ok) { const std::string v = wire_inv::Violation(s); if (!v.empty()) { std::printf("FAIL: invariant violated (%s) for: %.60s\n", v.c_str(), d.c_str()); ++fails; } }
    return ok;
}
static std::string H = "MCRADAR 4\n";

int main()
{
    // ---- version header: exact integer ----
    {
        WorldSnapshot s;
        CHECK(Parse("MCRADAR 4\n", s) && Parse("MCRADAR 4", s) && Parse("MCRADAR 4\r\n", s) && Parse("MCRADAR 1\nL A 1 2 3 4\n", s) && Parse("MCRADAR 3\n", s));
        CHECK(Parse("MCRADAR 4 extra text\n", s));                    // trailing words are ignored (room for later headers)
        CHECK(!Parse("MCRADAR 10\n", s) && !Parse("MCRADAR 40\n", s) && !Parse("MCRADAR 4junk\n", s) && !Parse("MCRADAR 41\n", s));   // used to parse as v1 / v4
        CHECK(!Parse("MCRADAR 5\n", s) && !Parse("MCRADAR 0\n", s) && !Parse("MCRADAR \n", s) && !Parse("MCRADAR\n", s) && !Parse("mcradar 4\n", s) && !Parse(" MCRADAR 4\n", s));
        CHECK(!Parse("MCRADAR 1000\n", s) && !Parse("MCRADAR -4\n", s) && !Parse("", s) && !Parse("\n", s));
    }

    // ---- numbers: only plain finite decimals ----
    {
        const char* bad[] = {"nan", "-nan", "NaN", "inf", "-inf", "Infinity", "1e999", "-1e999", "0x1p3", "0x10", "+1", "1,5", "1.5.5", "--1", "1e", "e5", ".", "1e-", "٣", "1e1e1", "-.", "1-1"};
        for (const char* v : bad)
        {
            WorldSnapshot s;
            Parse(H + "L A " + v + " 2 3\nP B 1 " + v + " 3\nP C 1 2 3 " + v + " " + v + " " + v + " " + v + " " + v + "\n", s);
            CHECK(!s.localValid);                                        // x is bad: the whole L line is dropped
            CHECK(s.players.size() == 1 && s.players[0].name == "C");    // B (bad y) dropped; C's optional columns are just "unknown"
            if (s.players.size() == 1) CHECK(!s.players[0].yawValid && !s.players[0].pitchValid && !s.players[0].healthValid && !s.players[0].sizeValid);
            CHECK(s.ignoredLines == 2);
        }
        const char* good[] = {"1", "-1", "0", "-0", "1.5", "-1.5", ".5", "5.", "1e3", "1E3", "1.5e-3", "-2.5E+2"};
        (void)good;
        WorldSnapshot s;
        CHECK(Parse(H + "L A 1e3 -2.5e2 .5 90 -45 20 0.6 1.8 u\n", s) && s.localValid && s.local.pos.x == 1000.0f && s.local.pos.y == -250.0f && s.local.pos.z == 0.5f && s.local.yawValid && s.local.sizeValid);
        // ranges
        CHECK(Parse(H + "L A 99999999 0 0\n", s) && s.localValid);
        CHECK(Parse(H + "L A 100000001 0 0\n", s) && !s.localValid);
        CHECK(Parse(H + "L A 0 0 0 1000000 -1000000 100000 64 64 u\n", s) && s.local.yawValid && s.local.pitchValid && s.local.healthValid && s.local.sizeValid);
        CHECK(Parse(H + "L A 0 0 0 1000001 -1000001 100001 65 1 u\n", s) && !s.local.yawValid && !s.local.pitchValid && !s.local.healthValid && !s.local.sizeValid);
        CHECK(Parse(H + "L A 0 0 0 - - -1 0 0 u\n", s) && !s.local.healthValid && !s.local.sizeValid);
    }

    // ---- camera: fov and mode ----
    {
        WorldSnapshot s;
        const char* fovOk[] = {"1", "30", "70", "110", "179"};
        for (const char* f : fovOk) CHECK(Parse(H + "C 1 2 3 4 5 " + f + " 1\n", s) && s.camera.valid && s.camera.fovValid);
        const char* fovBad[] = {"0", "0.5", "180", "360", "-70", "nan", "inf", "1e9", "-", "x"};
        for (const char* f : fovBad) CHECK(Parse(H + "C 1 2 3 4 5 " + f + " 1\n", s) && s.camera.valid && !s.camera.fovValid);     // the configured FOV is used instead
        CHECK(Parse(H + "C 1 2 3 4 5 70 2\n", s) && s.camera.mode == 2);
        CHECK(Parse(H + "C 1 2 3 4 5 70 3\n", s) && s.camera.mode == 3);
        const char* modeBad[] = {"0", "4", "-1", "99999999999999999999", "x", "-", "1.5", "nan"};
        for (const char* m : modeBad) CHECK(Parse(H + "C 1 2 3 4 5 70 " + m + "\n", s) && s.camera.mode == 1);
        CHECK(Parse(H + "C nan 2 3 4 5 70 1\n", s) && !s.camera.valid && s.ignoredLines == 1);
    }

    // ---- item count, enchant levels, T stamp ----
    {
        WorldSnapshot s;
        const struct { const char* c; int want; } cnt[] = {{"1", 1}, {"64", 64}, {"0", 1}, {"-5", 1}, {"nan", 1}, {"inf", 1}, {"-", 1}, {"1e30", 1000000}, {"99999999999", 1000000}, {"2.7", 2}, {"1000000", 1000000}, {"1000001", 1000000}};
        for (const auto& c : cnt) CHECK(Parse(H + "I 1 2 3 - - " + c.c + " k - -\n", s) && s.items.size() == 1 && s.items[0].count == c.want);   // nan / 1e30 were undefined behaviour
        CHECK(Parse(H + "I 1 2 3 - - 1 k a@99999999999999999999,b@-99999999999999999999,c@7 -\n", s) && s.items.size() == 1);
        std::string longEnch;
        for (int i = 0; i < 100; ++i) longEnch += "e" + std::to_string(i) + "@1,";
        CHECK(Parse(H + "I 1 2 3 - - 1 k " + longEnch + " -\n", s) && s.items.size() == 1 && s.items[0].enchants.size() == wire::kMaxEnchants);
        CHECK(Parse(H + "T 123456789\n", s) && s.hasSampleTime && s.sampleNs == 123456789);
        CHECK(Parse(H + "T -5\n", s) && s.hasSampleTime && s.sampleNs == -5);
        CHECK(Parse(H + "T 99999999999999999999\n", s) && !s.hasSampleTime && s.ignoredLines == 1);
        CHECK(Parse(H + "T abc\nT\n", s) && !s.hasSampleTime);
        CHECK(Parse(H + "T 1.5\n", s) && !s.hasSampleTime);
    }

    // ---- limits ----
    {
        WorldSnapshot s;
        std::string many = H;
        for (int i = 0; i < 300; ++i) many += "P p" + std::to_string(i) + " 1 2 3\n";
        CHECK(Parse(many, s) && s.players.size() == wire::kMaxPlayers && s.players[255].name == "p255" && s.ignoredLines == 44);
        std::string items = H + "F 1\n";
        for (int i = 0; i < 2000; ++i) items += "I 1 2 3 - - 1 k - -\n";
        CHECK(Parse(items, s) && s.items.size() == wire::kMaxItems && s.ignoredLines == 2000 - 1024);
        CHECK(Parse(H + "L " + std::string(100, 'n') + " 1 2 3\n", s) && s.local.name == std::string(wire::kMaxNameBytes, 'n'));
        CHECK(Parse(H + "S " + std::string(1000, 's') + "\n", s) && s.status.size() == wire::kMaxStatusBytes);
        CHECK(Parse(H + "L a 1 2 3 - - - - - " + std::string(100, 'u') + "\n", s) && s.local.uuid.size() == wire::kMaxUuidBytes);
        CHECK(Parse(H + "I 1 2 3 - - 1 " + std::string(500, 'k') + " - " + std::string(10000, 't') + "\n", s) && s.items[0].key.size() == wire::kMaxKeyBytes && s.items[0].tags.size() == wire::kMaxTagsBytes);
        CHECK(Parse(H + "S short\n", s) && s.status == "short");
        CHECK(Parse(H, s) && s.status == "agent connected");            // default when the agent sent none
    }

    // ---- text: UTF-8 and control characters ----
    {
        using wire::CleanText;
        CHECK(CleanText("plain", 100) == "plain" && CleanText("", 100).empty());
        CHECK(CleanText("J\xC3\xBCrgen \xD0\x96 \xE2\x82\xAC \xF0\x9F\x98\x80", 100) == "J\xC3\xBCrgen \xD0\x96 \xE2\x82\xAC \xF0\x9F\x98\x80");   // 2, 3, 4-byte sequences survive
        CHECK(CleanText("\xFF", 100) == "\xEF\xBF\xBD" && CleanText("a\xFEz", 100) == "a\xEF\xBF\xBDz");
        CHECK(CleanText("\xC0\xAF", 100) == "\xEF\xBF\xBD\xEF\xBF\xBD");                 // overlong '/'
        CHECK(CleanText("\xE0\x80\x80", 100) == "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD");   // overlong NUL
        CHECK(CleanText("\xED\xA0\x80", 100).find("\xED") == std::string::npos);          // UTF-16 surrogate
        CHECK(CleanText("\xF4\x90\x80\x80", 100).find("\xF4") == std::string::npos);      // above U+10FFFF
        CHECK(CleanText("\xF8\x88\x80\x80\x80", 100).find("\xF8") == std::string::npos);  // 5-byte form
        CHECK(CleanText("ab\xE2\x82", 100) == "ab\xEF\xBF\xBD\xEF\xBF\xBD");              // truncated sequence at the end
        CHECK(CleanText("\xC3(", 100) == "\xEF\xBF\xBD(");                                 // lead byte followed by ASCII keeps the ASCII
        CHECK(CleanText(std::string("a\0b", 3), 100) == "a?b" && CleanText("a\x01\x1B\x7F" "b", 100) == "a???b" && CleanText("tab\there", 100) == "tab?here");
        // truncation never splits a character
        CHECK(CleanText("ab\xE2\x82\xAC", 4) == "ab" && CleanText("ab\xE2\x82\xAC", 5) == "ab\xE2\x82\xAC" && CleanText("abcdef", 3) == "abc" && CleanText("\xF0\x9F\x98\x80", 3).empty());
        WorldSnapshot s;
        CHECK(Parse(H + "L \xFF\xFE\x01x 1 2 3\nP \xC3\x28 1 2 3\nS red\x1B[31m\n", s) && s.local.name == "\xEF\xBF\xBD\xEF\xBF\xBD?x" && s.players[0].name == "\xEF\xBF\xBD(" && s.status == "red?[31m");
        CHECK(Parse(H + "I 1 2 3 - - 1 k%FF%00x - %01%7B%22a%22%7D\n", s) && s.items[0].key == "k\xEF\xBF\xBD?x" && s.items[0].tags == "?{\"a\"}");   // percent escapes are decoded first, then cleaned
    }

    // ---- unchanged behaviour: out untouched on rejection, last L wins, CRLF, unknown tags, missing columns ----
    {
        WorldSnapshot keep;
        keep.status = "kept";
        CHECK(!Parse("garbage", keep) && keep.status == "kept");
        WorldSnapshot s;
        CHECK(Parse(H + "L A 1 2 3\r\nL B 4 5 6\r\nZ junk\r\nS ok\r\nP\r\nP only 1 2\r\n", s) && s.local.name == "B" && s.status == "ok" && s.players.empty() && s.ignoredLines == 1);   // 'Z junk' (unknown tag) and a bare 'P' are skipped silently; 'P only 1 2' is counted
        CHECK(Parse("MCRADAR 2\nL A 1 2 3 90 10 15\n", s) && s.local.yawValid && s.local.healthValid && s.local.health == 15.0f && !s.local.sizeValid);
        CHECK(Parse("MCRADAR 1\nL A 1 2 3 90\nC 1 2 3 4 5 70 1\nI 1 2 3 - - 1 k - -\n", s) && !s.camera.valid && s.items.empty());   // C and I need v3
        CHECK(Parse(H + "L A\t1\t2\t3\n", s) && s.localValid);                      // tabs separate columns too
    }

    std::printf(fails ? "\n%d FAILURES\n" : "\nALL OK\n", fails);
    return fails;
}
