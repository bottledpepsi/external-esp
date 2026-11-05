// perf_stats_test - PerfStats.h: the cache hit rate between two readings.
#include "PerfStats.h"
#include "golden.h"
#include <limits>
#include <vector>
#include <cmath>
#include <cstdio>
using namespace perf;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
static bool Near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

int main()
{
    CacheCounters a{100, 10, 0}, b{190, 20, 1};
    CHECK(Near(HitRatePct(a, b), 90.f)); // 90 hits and 10 misses in between
    CHECK(HitRatePct(b, b) == -1.f); // no lookup in the window
    CHECK(Near(HitRatePct({}, {50, 0, 0}), 100.f) && Near(HitRatePct({}, {0, 50, 0}), 0.f));
    CHECK(Near(HitRatePct({1000, 500, 4}, {30, 10, 0}), 75.f)); // the counters went backwards: read as since zero
    CHECK(Near(HitRatePct({1000, 500, 4}, {1000, 10, 0}), 100.f * 1000.f / 1010.f)); // one of them went backwards: also since zero
    CHECK(Near(HitRatePct({0xFFFFFFFFFFull, 0, 0}, {0xFFFFFFFFFFull + 5, 5, 0}), 50.f)); // large counts keep their precision where it matters
    // ---- Ring ----
    {
        Ring<4> r;
        CHECK(r.Size() == 0 && r.Summarize().n == 0 && r.Summarize().max == 0);
        for (float v : {1.f, 2.f, 3.f}) r.Push(v);
        CHECK(r.Size() == 3 && r.At(0) == 1 && r.At(2) == 3);
        for (float v : {4.f, 5.f, 6.f}) r.Push(v); // 1 and 2 fall out
        CHECK(r.Size() == 4 && r.At(0) == 3 && r.At(3) == 6);
        const RingStats s = r.Summarize();
        CHECK(s.n == 4 && s.min == 3 && s.max == 6 && Near(s.avg, 4.5f) && s.p95 == 5); // index 0.95*3 = 2 of 3 4 5 6
        r.Push(std::numeric_limits<float>::quiet_NaN());
        r.Push(-5.f);
        r.Push(std::numeric_limits<float>::infinity());
        CHECK(r.Summarize().max == 6 && r.Summarize().min == 0); // junk is stored as 0, never poisons the scale
        r.Clear();
        CHECK(r.Size() == 0);
        Ring<240> big;
        for (int i = 0; i < 100000; ++i) big.Push(static_cast<float>(i % 17)); // wraps many times without drifting
        CHECK(big.Size() == 240 && big.Summarize().max == 16);
    }
    CHECK(NiceCeiling(0) == 4 && NiceCeiling(4) == 4 && NiceCeiling(4.01f) == 8 && NiceCeiling(17) == 32 && NiceCeiling(1e12f) > 1e5f);

    // ---- JitterMeter == the golden metric ----
    {
        // a smooth path with a stutter every 7th frame and a gap (target out of view) in the middle
        std::vector<std::pair<double, double>> pts;
        for (int i = 0; i < 200; ++i)
        {
            const double x = 300 + 3.0 * i + ((i % 7) == 0 ? 2.5 : 0), y = 500 + 1.5 * i - ((i % 11) == 0 ? 1.0 : 0);
            const bool gap = i >= 90 && i < 94;
            pts.push_back(gap ? std::pair<double, double>{std::nan(""), std::nan("")} : std::pair<double, double>{x, y});
        }
        JitterMeter m;
        for (const auto& p : pts)
        {
            if (std::isnan(p.first)) m.PushMissing();
            else m.Push(p.first, p.second);
        }
        const golden::Metrics g = golden::Jitter(pts);
        const JitterStats j = m.Get();
        CHECK(j.n == static_cast<int>(g.frames)); // the same frames count: three valid neighbours each
        CHECK(Near(static_cast<float>(j.rms), static_cast<float>(g.jitterRmsPx)) && Near(static_cast<float>(j.max), static_cast<float>(g.jitterMaxPx)));
        CHECK(Near(static_cast<float>(j.p95), static_cast<float>(g.jitterP95Px)));
        CHECK(j.rms > 0.5); // and the stutter is actually visible in it
        // smooth motion: about 0
        JitterMeter smooth;
        for (int i = 0; i < 100; ++i) smooth.Push(10 + 4.0 * i, 20 + 2.0 * i);
        CHECK(smooth.Get().n == 98 && smooth.Get().rms < 1e-6 && smooth.Get().max < 1e-4);
        // two points are not enough; a missing frame restarts the chain; NaN counts as missing
        JitterMeter few;
        few.Push(1, 1);
        few.Push(2, 2);
        CHECK(few.Get().n == 0);
        few.PushMissing();
        few.Push(3, 3);
        few.Push(4, 4);
        CHECK(few.Get().n == 0);
        few.Push(std::nan(""), 5);
        few.Push(5, 5);
        few.Push(6, 6);
        CHECK(few.Get().n == 0);
        few.Push(7, 7);
        CHECK(few.Get().n == 1);
        // the window keeps the last 240 deviations
        JitterMeter w;
        for (int i = 0; i < 1000; ++i) w.Push(i * 1.0, (i % 2) ? 5.0 : 0.0);
        CHECK(w.Get().n == JitterMeter::kWindow && w.Get().max > 4.9);
        w.Clear();
        CHECK(w.Get().n == 0);
    }

    // ---- PerfHistory texts ----
    {
        PerfHistory h;
        h.Refresh();
        CHECK(h.frameText.empty() && h.ageText.empty() && h.jitterText == "jitter: no player in view");
        for (int i = 0; i < 100; ++i)
        {
            h.frameMs.Push(i % 10 == 0 ? 33.f : 8.f);
            h.cpuMs.Push(0.5f);
            h.ageMs.Push(6.f);
            h.jitter.Push(i * 3.0, i * 1.0);
        }
        h.Refresh();
        CHECK(h.frameText.find("frame ") == 0 && h.frameText.find("max 33.0") != std::string::npos && h.frameText.find("cpu 0.50 ms") != std::string::npos);
        CHECK(h.ageText.find("data age 6.0 ms") == 0);
        CHECK(h.jitterText.find("jitter (nearest player) rms 0.00 px") == 0);
        h.Clear();
        CHECK(h.frameMs.Size() == 0 && h.frameText.empty() && h.jitter.Get().n == 0);
    }

    if (fails) std::printf("%d failure(s)\n", fails);
    else std::printf("perf_stats_test: ok\n");
    return fails ? 1 : 0;
}
