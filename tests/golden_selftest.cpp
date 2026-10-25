// Tests of the golden harness itself (metric maths, JSON, comparison, a tiny end-to-end recording). The shipped recordings and their
// golden numbers are checked by golden_runner (see tests/CMakeLists.txt and tests/data).
#include "EspRec.h"
#include "golden.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
static bool near(double a, double b, double e) { return std::fabs(a - b) <= e; }

using namespace golden;

int main()
{
    const double nan = std::nan("");

    // ---- the metric ----
    {
        std::vector<std::pair<double, double>> lin, stutter, withGap;
        for (int i = 0; i < 100; ++i) lin.push_back({100 + 3.0 * i, 50 - 1.5 * i});          // constant velocity: dev = 0
        Metrics m = Jitter(lin);
        CHECK(m.frames == 98 && near(m.jitterRmsPx, 0, 1e-9) && near(m.jitterMaxPx, 0, 1e-9) && near(m.maxStepPx, std::hypot(3, 1.5), 1e-9));
        for (int i = 0; i < 100; ++i) stutter.push_back({100 + 3.0 * i + (i % 2 ? 2.0 : 0.0), 50});   // +2 px on every other frame
        m = Jitter(stutter);
        CHECK(near(m.jitterMaxPx, 2.0 * 1.5 - 0.0, 1.6));                                      // |p_i - midpoint| is 1..2 px for this pattern
        CHECK(m.jitterRmsPx > 0.9 && m.jitterRmsPx < 2.1 && m.jitterP95Px >= m.jitterRmsPx * 0.9);
        withGap = lin;
        withGap[40] = {nan, nan};                                                              // not visible for one frame: its 3 neighbours' windows are skipped
        m = Jitter(withGap);
        CHECK(m.frames == 95 && near(m.jitterRmsPx, 0, 1e-9));
        CHECK(Jitter(lin, 60).frames == 39);                                                   // warm-up skip
        CHECK(Jitter({}).frames == 0 && Jitter({{1, 1}, {2, 2}}).frames == 0);                  // too short: no crash, no data
        // a single teleport of 30 px: one big deviation, big step
        std::vector<std::pair<double, double>> tele = lin;
        for (size_t i = 50; i < tele.size(); ++i) tele[i].first += 30;
        m = Jitter(tele);
        CHECK(near(m.jitterMaxPx, 15, 0.01) && m.maxStepPx > 30);
    }

    // ---- JSON round trip ----
    {
        Options o;
        Result r;
        r["world_anchor"] = Metrics{1000, 0.0123, 0.03, 0.2, 0.9};
        r["player:Bob"] = Metrics{990, 1.5, 3.0, 9.0, 12.0};
        const std::string j = ToJson("x.esprec", o, r);
        CHECK(j.find("\"probe/player:Bob/jitter_rms_px\": 1.5000") != std::string::npos);
        Options o2;
        o2.frameHz = 1;
        Result r2;
        std::string err;
        CHECK(FromJson(j, o2, r2, &err) && o2.frameHz == 240 && o2.width == 1920 && r2.size() == 2);
        CHECK(near(r2["player:Bob"].jitterMaxPx, 9.0, 1e-9) && near(r2["world_anchor"].jitterRmsPx, 0.0123, 1e-9) && r2["world_anchor"].frames == 1000);
        CHECK(!FromJson("{\"format\": 2}", o2, r2, &err) && !FromJson("not json", o2, r2, &err));
    }

    // ---- comparison: only worsening fails ----
    {
        Result g;
        g["p"] = Metrics{1000, 1.0, 2.0, 5.0, 10.0};
        Tolerance tol;
        std::string notes;
        Result same = g;
        CHECK(Compare(g, same, tol, &notes).empty());
        Result slightlyWorse = g;
        slightlyWorse["p"].jitterRmsPx = 1.0 * 1.10 + 0.05 - 0.001;        // inside +10% +0.05
        CHECK(Compare(g, slightlyWorse, tol, &notes).empty());
        slightlyWorse["p"].jitterRmsPx = 1.0 * 1.10 + 0.05 + 0.01;         // just outside
        const std::string f = Compare(g, slightlyWorse, tol, &notes);
        CHECK(!f.empty() && f.find("jitter_rms") != std::string::npos);
        Result better = g;
        better["p"].jitterRmsPx = 0.2;
        notes.clear();
        CHECK(Compare(g, better, tol, &notes).empty() && notes.find("improved") != std::string::npos);   // improvement passes, is noted
        Result worseStep = g;
        worseStep["p"].maxStepPx = 25;
        CHECK(Compare(g, worseStep, tol, &notes).find("max_step") != std::string::npos);
        Result fewerFrames = g;
        fewerFrames["p"].frames = 900;
        CHECK(Compare(g, fewerFrames, tol, &notes).find("frames") != std::string::npos);
        Result missing;
        CHECK(Compare(g, missing, tol, &notes).find("not measured") != std::string::npos);
        Result extra = g;
        extra["new"] = Metrics{10, 0, 0, 0, 0};
        CHECK(Compare(g, extra, tol, &notes).find("not in the golden file") != std::string::npos);
        Result zero;
        zero["z"] = Metrics{100, 0, 0, 0, 0};
        Result z2 = zero;
        z2["z"].jitterRmsPx = 0.04;                                           // golden 0 -> the absolute slack applies
        CHECK(Compare(zero, z2, tol, &notes).empty());
        z2["z"].jitterRmsPx = 0.2;
        CHECK(!Compare(zero, z2, tol, &notes).empty());
    }

    // ---- end to end on a tiny recording: static camera, one player walking at 4 m/s in 50 ms ticks, agent at 120 Hz ----
    {
        std::ofstream f("tiny.esprec", std::ios::binary);
        esprec::Writer w(f);
        esprec::Header h;
        h.wireVersion = 4;
        CHECK(w.WriteHeader(h));
        const int N = 480;                                                     // 4 s at 120 Hz
        for (int i = 0; i < N; ++i)
        {
            const double tSec = i / 120.0;
            const double tick = std::floor(tSec * 20.0) / 20.0;                // the game only moves entities per 50 ms tick
            char b[512];
            std::snprintf(b, sizeof b,
                          "MCRADAR 4\nT %lld\nS ok\nC 0.0000 65.6200 0.0000 0.00 0.00 70.00 1\nL Me 0.000 64.000 0.000 0.00 0.00 20.00 0.60 1.80 -\n"
                          "P Bob %.3f 64.000 10.000 0.00 0.00 20.00 0.60 1.80 -\n",
                          static_cast<long long>(5000000000LL + i * 8333333LL), -4.0 + 4.0 * tick);
            CHECK(w.Append(static_cast<uint64_t>(i) * 8333333ULL, b, std::strlen(b)));
        }
        w.Flush();
        f.close();
        Result r;
        std::string err;
        CHECK(Measure("tiny.esprec", Options{}, r, &err));
        CHECK(r.count("world_anchor") && r.count("player:Bob"));
        CHECK(near(r["world_anchor"].jitterMaxPx, 0, 1e-3) && near(r["world_anchor"].maxStepPx, 0, 1e-3));   // static camera: the anchor never moves
        CHECK(r["player:Bob"].frames > 500 && r["player:Bob"].maxStepPx > 0 && r["player:Bob"].maxStepPx < 20);   // moves, no teleports
        std::printf("tiny walk: player rms %.3f px, p95 %.3f px, max %.3f px, step %.3f px (%.0f frames)\n", r["player:Bob"].jitterRmsPx,
                    r["player:Bob"].jitterP95Px, r["player:Bob"].jitterMaxPx, r["player:Bob"].maxStepPx, r["player:Bob"].frames);
        Result again;
        CHECK(Measure("tiny.esprec", Options{}, again, &err) && ToJson("t", Options{}, again) == ToJson("t", Options{}, r));   // deterministic
        CHECK(!Measure("missing.esprec", Options{}, again, &err) && !err.empty());
    }

    std::printf(fails ? "\n%d FAILURES\n" : "\nALL OK\n", fails);
    return fails;
}
