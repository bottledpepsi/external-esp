#include <cstring>
#include "Camera.h"
#include "ScreenBox.h"
#include "ItemText.h"
#include "EspConfig.h"
#include "FriendList.h"
#include "Presets.h"
#include "ProfileManager.h"
#include "Smoother.h"
#include "Wire.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
static bool near(double a, double b, double e = 1e-3) { return std::fabs(a - b) <= e; }
// equal as angles (the smoother returns an unwrapped yaw, e.g. 181 for -179)
static bool angNear(double a, double b, double e = 1e-2) { double d = std::fmod(a - b, 360.0); if (d > 180) d -= 360; if (d < -180) d += 360; return std::fabs(d) <= e; }

int main() {
    // --- JSON from the ORIGINAL C# tool (PascalCase, 2.0 floats, extra unknown key) loads
    std::string csharp = R"({"HotkeyToggle":"F6","HotkeyConfig":"Insert","HotkeyExit":"End","DistanceFormat":"Kilometers",
      "MaxLabelDist":50,"TextShadow":false,"BoxStyle":"Corner-only","CornerRadius":6.0,"BoxThickness":3,"BoxColorMode":"Gradient",
      "NameSize":17,"BoxFillOpacity":0.2,"TracerOrigin":"Center","TracerColor":"White","TracerOpacity":0.5,"TracerTarget":"Head",
      "MaxPlayerDist":100,"MinimapSize":220,"MinimapRange":50,"OverlayOpacity":0.85,"FpsCap":120,"SomethingNew":{"a":[1,2]}})";
    auto o = json::ParseObject(csharp);
    CHECK(o.has_value());
    Settings s; s.Apply(EspConfig::FromJson(*o));
    CHECK(s.vkToggle == 0x75); CHECK(s.distFmtIdx == 1); CHECK(s.labelDistIdx == 1); CHECK(!s.textShadow);
    CHECK(s.boxStyleIdx == 2); CHECK(s.cornerRadiusIdx == 2); CHECK(s.boxThickIdx == 2); CHECK(s.playerColorIdx == 1);
    CHECK(s.nameSizeIdx == 2); CHECK(s.boxFillIdx == 2); CHECK(s.tracerOriginIdx == 1); CHECK(s.tracerColorIdx == 1);
    CHECK(s.tracerOpacityIdx == 1); CHECK(s.tracerTargetIdx == 2); CHECK(s.maxPlayerDistIdx == 1);
    CHECK(s.minimapSizeIdx == 2); CHECK(s.minimapRangeIdx == 0); CHECK(s.overlayOpacityIdx == 1); CHECK(s.fpsCap == 1);
    CHECK(s.fovIdx == 1 && s.vsync);                       // keys absent in the C# file -> defaults

    // --- Build -> JSON -> parse -> Apply is lossless
    EspConfig built = s.Build();
    auto o2 = json::ParseObject(built.ToJson());
    CHECK(o2.has_value());
    Settings s2; s2.Apply(EspConfig::FromJson(*o2));
    CHECK(s2.Build().ToJson() == built.ToJson());
    CHECK(built.MaxLabelDist == 50 && built.BoxStyle == "Corner-only" && built.HotkeyToggle == "F6");
    Settings d; CHECK(d.Build().MaxLabelDist == -1 && d.Build().MaxPlayerDist == -1 && d.Build().FpsCap == 0);

    // --- Unlimited sentinel + malformed JSON
    std::string err; CHECK(!json::ParseObject("{\"a\": }", &err).has_value()); CHECK(!err.empty());
    CHECK(!json::ParseObject("not json").has_value());

    // --- Settings save/load on disk
    s.Save(); Settings s3; s3.Load(); CHECK(s3.Build().ToJson() == s.Build().ToJson());

    // --- Friends: persistence, case-insensitive, blanks
    FriendList::Load();
    CHECK(FriendList::Add("Bob")); CHECK(!FriendList::Add("bob")); CHECK(!FriendList::Add("   ")); CHECK(FriendList::Add("alice"));
    CHECK(FriendList::Contains("BOB")); CHECK(FriendList::Names().size() == 2);
    FriendList::Remove("alice"); CHECK(!FriendList::Contains("alice"));
    { std::ifstream f("friends.json"); std::stringstream ss; ss << f.rdbuf(); CHECK(ss.str().find("Bob") != std::string::npos); }

    // --- Profiles: sanitise, save, load, list, delete
    ProfileManager::Refresh();
    CHECK(ProfileManager::Save("My Profile!<>", s.Build()));         // -> "My Profile"
    CHECK(!ProfileManager::Save("<<>>", s.Build()));
    CHECK(ProfileManager::ProfileNames().size() == 1 && ProfileManager::ProfileNames()[0] == "My Profile");
    auto lp = ProfileManager::Load("My Profile"); CHECK(lp && lp->BoxStyle == "Corner-only");
    CHECK(!ProfileManager::Load("nope")); CHECK(ProfileManager::Delete("My Profile")); CHECK(ProfileManager::ProfileNames().empty());

    // --- Presets
    CHECK(presets::VkToName(0x2E) == "Delete" && presets::NameToVk("insert") == 0x2D && presets::NameToVk("q") == 'Q' && presets::NameToVk("0x75") == 0x75);
    CHECK(presets::IsJunkItem("rotten flesh") && !presets::IsJunkItem("Diamond"));

    // --- Wire: a REAL datagram from the agent (v2), v1 and garbage
    WorldSnapshot w;
    const char* v2 = "MCRADAR 2\nS agent ok: 2 other player(s)\nL Alice 97.213 64.000 -45.849 -9.24 20.00 17.50\nP Bob 110.000 64.000 -41.672 45.00 10.00 -\nP Carol 90 70 -60 - - -\n";
    CHECK(ParseDatagram(v2, std::strlen(v2), w));
    CHECK(w.localValid && w.local.name == "Alice" && near(w.local.pos.z, -45.849) && w.local.healthValid && near(w.local.health, 17.5) && near(w.local.pitchDeg, 20));
    CHECK(w.players.size() == 2 && !w.players[0].healthValid && w.players[0].pitchValid && !w.players[1].yawValid);
    const char* v1 = "MCRADAR 1\nS ok\nL A 1 2 3 45.5\nP B 4 5 6 -\n";
    CHECK(ParseDatagram(v1, std::strlen(v1), w) && near(w.local.yawDeg, 45.5) && !w.local.pitchValid && w.players.size() == 1);
    CHECK(!ParseDatagram("garbage\n", 8, w));

    // --- Camera: yaw 0 faces +Z; player's right is -X
    Camera c; c.eye = {0, 1.62, 0}; c.yawDeg = 0; c.pitchDeg = 0; c.fovYDeg = 70; c.w = 1920; c.h = 1080; c.Update();
    float x, y;
    CHECK(c.Project({0, 1.62, 10}, x, y) && near(x, 960, 0.5) && near(y, 540, 0.5));      // dead ahead -> screen centre
    CHECK(c.Project({-3, 1.62, 10}, x, y) && x > 960);                                    // -X is to the RIGHT at yaw 0
    CHECK(c.Project({3, 1.62, 10}, x, y) && x < 960);
    CHECK(c.Project({0, 5.62, 10}, x, y) && y < 540);                                     // higher -> up on screen
    CHECK(!c.Project({0, 1.62, -10}, x, y));                                              // behind
    // known geometry: a point 10 m ahead, tan(35)=0.7002 -> vertical half-height 7.002 m maps to the top edge
    CHECK(c.Project({0, 1.62 + 10 * std::tan(35 * 3.14159265358979 / 180), 10}, x, y) && near(y, 0, 0.5));
    // yaw 90 faces -X : a target at -X is dead ahead
    c.yawDeg = 90; c.Update(); CHECK(c.Project({-10, 1.62, 0}, x, y) && near(x, 960, 0.5));
    // yaw -90 faces +X ; yaw 180 faces -Z
    c.yawDeg = -90; c.Update(); CHECK(c.Project({10, 1.62, 0}, x, y) && near(x, 960, 0.5));
    c.yawDeg = 180; c.Update(); CHECK(c.Project({0, 1.62, -10}, x, y) && near(x, 960, 0.5));
    // pitch +45 looks DOWN: a target below-ahead is centred
    c.yawDeg = 0; c.pitchDeg = 45; c.Update(); CHECK(c.Project({0, 1.62 - 10, 10}, x, y) && near(y, 540, 0.5));
    // up vector sanity (unit, perpendicular)
    CHECK(near(Camera::Dot(c.u, c.f), 0) && near(Camera::Dot(c.u, c.r), 0) && near(Camera::Dot(c.u, c.u), 1));

    // --- Smoother, entity positions: tick interpolation (matches how the game draws entities) ---------------------
    // Positions are tick-rate values. A new sample `b` 50 ms after `a` starts a glide a -> b that lasts one tick
    // (50 ms) and then HOLDS at b: it never extrapolates past the newest position. (Before the jitter rewrite this
    // test expected velocity extrapolation: +1.0 m at 50 ms and +1.5 m at 500 ms. That alternated between 0 and 3x
    // the true speed for tick-stepped data, which is the "boxes vibrate around a moving player" bug.)
    // Yaw wraps: 179 -> -179 glides through 180, not back through 0.
    Smoother sm; WorldSnapshot a, b; auto t0 = Smoother::Clock::now();
    PlayerState p; p.name = "Bob"; p.yawValid = true; p.pos = {0, 0, 0}; p.yawDeg = 179;
    a.players = {p}; a.localValid = true; a.local = p; a.arrived = t0; a.seq = 1;
    p.pos.x = 0.5; p.yawDeg = -179; b.players = {p}; b.localValid = true; b.local = p; b.arrived = t0 + std::chrono::milliseconds(50); b.seq = 2;
    sm.Push(a); sm.Push(b);
    auto pm = sm.Predict(b.arrived + std::chrono::milliseconds(25), true);                 // half way through the tick
    CHECK(near(pm.players[0].pos.x, 0.25, 1e-3)); CHECK(angNear(pm.players[0].yawDeg, 180));
    auto pr = sm.Predict(b.arrived + std::chrono::milliseconds(50), true);                 // glide finished
    CHECK(near(pr.players[0].pos.x, 0.5, 1e-3)); CHECK(angNear(pr.players[0].yawDeg, -179));
    CHECK(near(sm.Predict(b.arrived + std::chrono::milliseconds(500), true).players[0].pos.x, 0.5, 1e-3));   // holds, no overshoot
    CHECK(near(sm.Predict(b.arrived + std::chrono::milliseconds(25), false).players[0].pos.x, 0.5, 1e-9));   // disabled: latest raw sample

    // --- Wire v3: camera, hitbox, uuid, items (percent-encoded tags) -----------------------------------
    {
        const char* v3 =
            "MCRADAR 3\nS agent ok\n"
            "C 97.9276 65.6200 -45.4497 -175.83 18.91 68.02 2\n"
            "L Alice 100.000 64.000 -50.000 -175.83 18.91 20.00 0.60 1.80 8667d4ae-9e7c-3f2d-a1b2-c3d4e5f60718\n"
            "P Carol 90.000 70.000 -60.000 90.00 20.00 17.50 0.60 1.50 069a79f4-44e9-4726-a5be-fca90e38aaf5\n"
            "I 103.000 64.000 -47.000 0.25 0.25 1 minecraft:diamond_sword minecraft:sharpness@5,minecraft:unbreaking@3 %7BEnchantments:%5B%7Bid:%22minecraft:sharpness%22%7D%5D%7D\n"
            "I 98.000 64.000 -52.000 - - 37 minecraft:cobblestone - -\n";
        WorldSnapshot w3;
        CHECK(ParseDatagram(v3, std::strlen(v3), w3));
        CHECK(w3.camera.valid && w3.camera.fovValid && near(w3.camera.fovDeg, 68.02) && w3.camera.mode == 2 && near(w3.camera.pos.y, 65.62, 1e-3));
        CHECK(w3.localValid && w3.local.sizeValid && near(w3.local.height, 1.8) && w3.local.uuid == "8667d4ae-9e7c-3f2d-a1b2-c3d4e5f60718");
        CHECK(w3.players.size() == 1 && w3.players[0].sizeValid && near(w3.players[0].height, 1.5) && w3.players[0].uuid[14] == '4');
        CHECK(w3.items.size() == 2);
        CHECK(w3.items[0].key == "minecraft:diamond_sword" && w3.items[0].count == 1 && w3.items[0].sizeValid && w3.items[0].enchants.size() == 2);
        CHECK(w3.items[0].enchants[0].id == "minecraft:sharpness" && w3.items[0].enchants[0].level == 5 && w3.items[0].enchants[1].level == 3);
        CHECK(w3.items[0].tags == "{Enchantments:[{id:\"minecraft:sharpness\"}]}");           // %XX decoding incl. quotes/braces
        CHECK(w3.items[1].count == 37 && !w3.items[1].sizeValid && w3.items[1].enchants.empty() && w3.items[1].tags.empty());
        // v3 camera line without FOV, and a v2 packet still parses (no camera, no size)
        const char* nofov = "MCRADAR 3\nC 1 2 3 10 20 - 1\n";
        WorldSnapshot wn; CHECK(ParseDatagram(nofov, std::strlen(nofov), wn) && wn.camera.valid && !wn.camera.fovValid);
        const char* v2b = "MCRADAR 2\nL A 1 2 3 4 5 6\nP B 4 5 6 - - -\n";
        WorldSnapshot w2; CHECK(ParseDatagram(v2b, std::strlen(v2b), w2) && !w2.camera.valid && !w2.local.sizeValid && w2.local.uuid.empty());
    }

    // --- item text helpers --------------------------------------------------------------------------
    CHECK(PrettyItemName("minecraft:diamond_sword") == "Diamond Sword");
    CHECK(PrettyItemName("diamondSword") == "Diamond Sword");
    CHECK(PrettyItemName("minecraft:potion.effect.healing") == "Potion (Healing)");
    CHECK(PrettyItemName("mymod:ruby_ore") == "Ruby Ore");
    CHECK(PrettyEnchant({"minecraft:sharpness", 5}) == "Sharpness V");
    CHECK(PrettyEnchant({"minecraft:mending", 1}) == "Mending");
    CHECK(PrettyEnchant({"minecraft:binding_curse", 1}) == "Binding Curse" && IsCurse({"minecraft:binding_curse", 1}));
    CHECK(PrettyEnchant({"minecraft:efficiency", 12}) == "Efficiency 12");
    CHECK(PrettyEnchant({"16", 3}) == "Enchant #16 III");
    CHECK(PercentDecode("-").empty() && PercentDecode("a%20b") == "a b" && PercentDecode("100%") == "100%");
    CHECK(presets::IsJunkItem(PrettyItemName("minecraft:rotten_flesh")) && !presets::IsJunkItem(PrettyItemName("minecraft:diamond")));

    // --- new config keys round trip and default when absent (older files) --------------------------------
    {
        Settings a; a.showItemEnchants = false; a.showItemTags = true;
        auto o3 = json::ParseObject(a.Build().ToJson());
        Settings b; b.Apply(EspConfig::FromJson(*o3));
        CHECK(!b.showItemEnchants && b.showItemTags);
        auto old = json::ParseObject("{\"BoxStyle\":\"Sharp\"}");
        Settings c; c.Apply(EspConfig::FromJson(*old));
        CHECK(c.showItemEnchants && !c.showItemTags);
    }

    // --- Smoother extrapolates the camera too (and not across a perspective switch) ----------------------
    {
        Smoother sm2; WorldSnapshot a, b; auto t0 = Smoother::Clock::now();
        a.camera.valid = b.camera.valid = true; a.camera.mode = b.camera.mode = 1;
        a.camera.pos = {0, 0, 0}; b.camera.pos = {0.5, 0, 0}; a.camera.yawDeg = 179; b.camera.yawDeg = -179;
        a.arrived = t0; b.arrived = t0 + std::chrono::milliseconds(50);
        sm2.Push(a); sm2.Push(b);
        auto pr2 = sm2.Predict(b.arrived + std::chrono::milliseconds(50), true);
        CHECK(near(pr2.camera.pos.x, 1.0) && near(pr2.camera.yawDeg, -177, 1e-2));
        b.camera.mode = 2; Smoother sm3; sm3.Push(a); sm3.Push(b);
        CHECK(near(sm3.Predict(b.arrived + std::chrono::milliseconds(50), true).camera.pos.x, 0.5));
    }


    // --- Distant items must not vanish: a 0.25-block item at 100 blocks is ~2 px at screen centre -------------
    {
        Camera cc; cc.eye = {0, 1.62, 0}; cc.yawDeg = 0; cc.pitchDeg = 0; cc.fovYDeg = 70; cc.w = 1920; cc.h = 1080; cc.Update();
        ScreenBox sb;
        // raw size straight ahead at 100 blocks is below the old 2 px cull -> this is what used to hide the item
        ScreenBox raw;
        CHECK(ProjectBox(cc, {0, 1.62, 100}, 0.25, 0.25, 0.f, raw) && (raw.x1 - raw.x0) < 2.f);
        // now: still drawn, at the minimum marker size, centred on the item
        CHECK(ProjectBox(cc, {0, 1.62, 100}, 0.25, 0.25, 6.f, sb));
        CHECK(near(sb.x1 - sb.x0, 6.0, 1e-3) && near(sb.y1 - sb.y0, 6.0, 1e-3) && near((sb.x0 + sb.x1) * 0.5, 960, 1.0));
        // same at 300 blocks, and far off to one side (inside the view)
        CHECK(ProjectBox(cc, {0, 1.62, 300}, 0.25, 0.25, 6.f, sb));
        CHECK(ProjectBox(cc, {-120, 1.62, 100}, 0.25, 0.25, 6.f, sb));
        // near items keep their true (larger) size
        CHECK(ProjectBox(cc, {0, 1.62, 3}, 0.25, 0.25, 6.f, sb) && (sb.x1 - sb.x0) > 20.f);
        // behind the camera / outside the view are still rejected
        CHECK(!ProjectBox(cc, {0, 1.62, -20}, 0.25, 0.25, 6.f, sb));
        CHECK(!ProjectBox(cc, {500, 1.62, 10}, 0.25, 0.25, 6.f, sb));
        // consistency: an item is visible when looked at directly AND when it is near the edge of the view
        CHECK(ProjectBox(cc, {0, 1.62, 90}, 0.25, 0.25, 6.f, sb) && ProjectBox(cc, {-55, 1.62, 90}, 0.25, 0.25, 6.f, sb));
    }

    std::printf(fails ? "\n%d FAILURES\n" : "\nALL OK\n", fails);
    return fails;
}
