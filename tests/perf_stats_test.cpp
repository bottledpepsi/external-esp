// perf_stats_test - PerfStats.h: the cache hit rate between two readings.
#include "PerfStats.h"
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
    if (fails) std::printf("%d failure(s)\n", fails);
    else std::printf("perf_stats_test: ok\n");
    return fails ? 1 : 0;
}
