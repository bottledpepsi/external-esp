// smoother_tuning_test - the Smoother's constants as runtime settings: clamping, that each one does what its name says, and the error statistic.
#include "Smoother.h"
#include <cmath>
#include <cstdio>
#include <limits>
using namespace std::chrono;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

static const steady_clock::time_point T0 = steady_clock::now();
static steady_clock::time_point At(double s) { return T0 + duration_cast<steady_clock::duration>(duration<double>(s)); }

// a camera + one player sample taken at game time t (seconds)
static WorldSnapshot Sample(double t, double yaw, double px = 0, uint64_t seq = 1)
{
    WorldSnapshot s;
    s.seq = seq;
    s.camera.valid = true;
    s.camera.pos = {px, 65.6, 0};
    s.camera.yawDeg = static_cast<float>(yaw);
    s.camera.pitchDeg = 0;
    s.camera.fovValid = true;
    s.camera.fovDeg = 70;
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
    // ---- Sanitized: ranges, non-finite values ----
    {
        SmootherTuning t;
        t.baseline = 0; t.blendTau = -1; t.maxAhead = 5; t.tick = 0;
        const SmootherTuning s = t.Sanitized();
        CHECK(s.baseline == SmootherTuning::kBaselineMin && s.blendTau == 0 && s.maxAhead == SmootherTuning::kMaxAheadMax && s.tick == SmootherTuning::kTickMin);
        t.baseline = std::numeric_limits<double>::quiet_NaN();
        t.blendTau = std::numeric_limits<double>::infinity();
        const SmootherTuning n = t.Sanitized();
        CHECK(n.baseline == SmootherTuning{}.baseline && n.blendTau == SmootherTuning{}.blendTau);
        CHECK(SmootherTuning{}.Sanitized() == SmootherTuning{}); // the defaults are inside their own ranges
        Smoother sm;
        sm.SetTuning(t);
        CHECK(sm.Tuning() == n);
    }

    // a camera turning at a constant 100 deg/s, a sample every 8 ms
    auto Feed = [](Smoother& sm, int n, double dt, double rate)
    {
        for (int i = 0; i < n; ++i) sm.Push(Sample(i * dt, rate * i * dt, 0, static_cast<uint64_t>(i + 1)));
        return (n - 1) * dt;
    };

    // ---- maxAhead: 0 = no extrapolation (the newest sample, as is); the default extrapolates ----
    {
        Smoother a, b;
        SmootherTuning t;
        t.maxAhead = 0;
        b.SetTuning(t);
        const double last = Feed(a, 20, 0.008, 100);
        Feed(b, 20, 0.008, 100);
        const double yawLast = 100 * last;
        const auto pa = a.Predict(At(last + 0.006), true), pb = b.Predict(At(last + 0.006), true);
        CHECK(std::fabs(pb.camera.yawDeg - yawLast) < 1e-3);               // held at the newest sample
        CHECK(pa.camera.yawDeg - yawLast > 0.3 && pa.camera.yawDeg - yawLast < 0.9); // ~100 deg/s * 6 ms
    }

    // ---- blendTau: 0 = the display jumps to the new prediction; the default eases ----
    {
        Smoother a, b;
        SmootherTuning t;
        t.blendTau = 0;
        b.SetTuning(t);
        const double dt = 0.008;
        for (Smoother* sm : {&a, &b})
        {
            Feed(*sm, 20, dt, 100);
            sm->Predict(At(19 * dt + 0.004), true); // the frame shown before the next sample arrives
            sm->Push(Sample(20 * dt, 100 * 19 * dt + 8.0, 0, 21)); // the camera suddenly turned 8 deg more than predicted
        }
        const double tn = 20 * dt;
        const double target = 100 * 19 * dt + 8.0; // the new sample
        const auto pa = a.Predict(At(tn), true), pb = b.Predict(At(tn), true);
        CHECK(std::fabs(pb.camera.yawDeg - target) < 0.5);               // no blend: at (or very near) the new pose at once
        CHECK(pa.camera.yawDeg < pb.camera.yawDeg - 3.0);                // blend: still mostly on the old path
        const auto pa2 = a.Predict(At(tn + 0.040), true), pb2 = b.Predict(At(tn + 0.040), true); // 4 tau later the correction has decayed
        CHECK(std::fabs(pa2.camera.yawDeg - pb2.camera.yawDeg) < 0.5);
    }

    // ---- tick: the interpolation length of entity positions ----
    {
        Smoother a, b;
        SmootherTuning t;
        t.tick = 0.100;
        b.SetTuning(t);
        for (Smoother* sm : {&a, &b})
        {
            sm->Push(Sample(0.00, 0, 0.0, 1));
            sm->Push(Sample(0.05, 0, 1.0, 2)); // the player stepped 1 block at t = 50 ms
        }
        const auto pa = a.Predict(At(0.05 + 0.025), true), pb = b.Predict(At(0.05 + 0.025), true);
        CHECK(std::fabs(pa.players[0].pos.x - 0.5) < 1e-3);   // 20 TPS: half-way after half a tick
        CHECK(std::fabs(pb.players[0].pos.x - 0.25) < 1e-3);  // 10 TPS: a quarter of the way
    }

    // ---- statistic: ~0 for a perfectly predictable camera, > 0 for an unpredictable one, cuts not counted ----
    {
        Smoother a;
        Feed(a, 200, 0.008, 100);
        for (int i = 0; i < 200; ++i) a.Predict(At(i * 0.008 + 0.004), true); // frames in between (what the overlay does)
        CHECK(a.Stats().samples == 0 || a.Stats().camAngErrDeg < 1e-3);

        Smoother b;
        double yaw = 0;
        for (int i = 0; i < 200; ++i)
        {
            yaw += (i % 2 ? 1.0 : -0.4) * 2.0; // accelerating and braking: the straight-line guess is always a little off
            b.Push(Sample(i * 0.008, yaw, 0, static_cast<uint64_t>(i + 1)));
            b.Predict(At(i * 0.008 + 0.004), true);
        }
        CHECK(b.Stats().samples > 100 && b.Stats().camAngErrDeg > 0.2 && b.Stats().camAngErrDeg < 6.0);

        Smoother c; // one teleport-sized jump is a cut, not an error
        for (int i = 0; i < 30; ++i)
        {
            c.Push(Sample(i * 0.008, i == 15 ? 150 : 0.0, 0, static_cast<uint64_t>(i + 1)));
            c.Predict(At(i * 0.008 + 0.004), true);
        }
        CHECK(c.Stats().camAngErrDeg < 1.0);
    }

    if (fails) std::printf("%d failure(s)\n", fails); else std::puts("smoother_tuning_test: all passed");
    return fails ? 1 : 0;
}
