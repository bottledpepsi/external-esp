// view_test - the raw / smoothed view: the camera a snapshot describes, the Smoother's newest-sample accessor, the ViewMode setting and the HUD tag.
#include "Camera.h"
#include "ConfigSchema.h"
#include "EspConfig.h"
#include "Hotkeys.h"
#include "Hud.h"
#include "Smoother.h"
#include <cmath>
#include <cstdio>
using namespace std::chrono;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
static bool Near(double a, double b, double e = 1e-6) { return std::fabs(a - b) < e; }

static const steady_clock::time_point T0 = steady_clock::now();
static steady_clock::time_point At(double s) { return T0 + duration_cast<steady_clock::duration>(duration<double>(s)); }

static WorldSnapshot Sample(double t, double yaw, double px)
{
    WorldSnapshot s;
    s.seq = 1;
    s.camera.valid = true;
    s.camera.pos = {px, 65.6, 0};
    s.camera.yawDeg = static_cast<float>(yaw);
    s.camera.fovValid = true;
    s.camera.fovDeg = 85;
    PlayerState p;
    p.name = "bob";
    p.pos = {px, 64, 5};
    s.players.push_back(p);
    s.hasSampleTime = true;
    s.sampleNs = 5'000'000'000LL + static_cast<int64_t>(t * 1e9);
    s.arrived = At(t);
    return s;
}

int main()
{
    // ---- CameraFromSnapshot ----
    {
        WorldSnapshot s;
        Camera c;
        bool exact = true;
        CHECK(!CameraFromSnapshot(s, 70, 1920, 1080, c, &exact)); // nothing to build a camera from
        s.localValid = true;
        s.local.pos = {10, 64, -3};
        s.local.yawDeg = 90;
        s.local.pitchDeg = 20;
        s.local.yawValid = s.local.pitchValid = true;
        CHECK(CameraFromSnapshot(s, 72, 1280, 720, c, &exact) && !exact); // reconstructed from the player: eye above the feet, the fallback FOV
        CHECK(Near(c.eye.y, 64 + Camera::kEyeHeight) && c.yawDeg == 90 && c.pitchDeg == 20 && c.fovYDeg == 72 && c.w == 1280 && c.h == 720);
        CHECK(Near(c.k, 1.0 / std::tan(72 * 3.14159265358979323846 / 360.0))); // Update() ran
        s.local.pitchValid = false;
        CHECK(!CameraFromSnapshot(s, 70, 1920, 1080, c)); // yaw without pitch is not enough
        s.camera.valid = true;
        s.camera.pos = {1, 2, 3};
        s.camera.yawDeg = 10;
        s.camera.pitchDeg = 5;
        s.camera.fovValid = false;
        CHECK(CameraFromSnapshot(s, 66, 1920, 1080, c, &exact) && exact && c.eye.x == 1 && c.fovYDeg == 66); // the game's pose, the configured FOV
        s.camera.fovValid = true;
        s.camera.fovDeg = 101;
        CHECK(CameraFromSnapshot(s, 66, 1920, 1080, c) && c.fovYDeg == 101); // the game's own FOV wins
    }

    // ---- Smoother::Newest is the sample as it came; the predicted picture moves on ----
    {
        Smoother sm;
        sm.Push(Sample(0.00, 0, 0));
        sm.Push(Sample(0.05, 10, 1)); // moving: +1 block and +10 deg per 50 ms
        sm.Push(Sample(0.10, 20, 2));
        CHECK(sm.HasData());
        CHECK(sm.Newest().camera.pos.x == 2 && sm.Newest().camera.yawDeg == 20 && sm.Newest().players.size() == 1 && sm.Newest().players[0].pos.x == 2);
        WorldSnapshot pred;
        sm.PredictInto(At(0.13), true, pred);
        CHECK(pred.camera.yawDeg != sm.Newest().camera.yawDeg); // the smoothed camera was extrapolated; the raw one was not touched
        CHECK(sm.Newest().camera.yawDeg == 20 && sm.Newest().camera.pos.x == 2);
        WorldSnapshot off;
        sm.PredictInto(At(0.13), false, off);
        CHECK(off.camera.yawDeg == 20 && off.camera.pos.x == 2); // smoothing off = raw
    }

    // ---- the setting ----
    {
        Settings s;
        CHECK(s.viewMode == 0);
        for (int m = 0; m < 3; ++m)
        {
            s.viewMode = m;
            const auto back = configschema::LoadText(s.Build().ToJson());
            Settings t;
            t.Apply(back.cfg);
            CHECK(back.Warnings() == 0 && t.viewMode == m);
        }
        CHECK(configschema::LoadText(R"({"Version":1})").cfg.ViewMode == "Smoothed"); // an old file: the normal view
        const auto bad = configschema::LoadText(R"({"ViewMode":"Ghost"})");
        CHECK(bad.Warnings() == 1 && bad.cfg.ViewMode == "Smoothed");
        s.viewMode = 99; // never reached through the UI, but a garbage value must not break Build
        CHECK(s.Build().ViewMode == "Smoothed");
    }

    // ---- the hotkey ----
    {
        using namespace hotkeys;
        CHECK(std::string(Def(Action::CycleView).key) == "HotkeyCycleView" && !Def(Action::CycleView).required);
        EspConfig c;
        CHECK(&c.HotkeyText(Action::CycleView) == &c.HotkeyCycleView && &c.HotkeyText(Action::CycleHud) == &c.HotkeyCycleHud);
        CHECK(!Defaults()[static_cast<size_t>(Action::CycleView)].Bound()); // no default key: it could be one the game uses
    }

    // ---- the status text says which view is on ----
    {
        hud::Facts f;
        f.agentLive = true;
        f.haveCam = true;
        f.fps = 100;
        hud::Text t;
        t.Build(hud::Mode::Compact, f);
        CHECK(t.Line(0).find("RAW") == std::string::npos);
        f.view = 1;
        t.Build(hud::Mode::Compact, f);
        CHECK(t.Line(0).find("  |  RAW") != std::string::npos && t.Line(0).find("RAW+SMOOTHED") == std::string::npos);
        f.view = 2;
        t.Build(hud::Mode::Compact, f);
        CHECK(t.Line(0).find("RAW+SMOOTHED") != std::string::npos);
        t.Build(hud::Mode::Detailed, f);
        CHECK(t.Line(0).find("RAW+SMOOTHED") != std::string::npos);
    }

    if (fails) std::printf("%d failure(s)\n", fails);
    else std::printf("view_test: ok\n");
    return fails ? 1 : 0;
}
