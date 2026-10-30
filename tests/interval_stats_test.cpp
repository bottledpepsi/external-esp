// interval_stats_test.cpp - same data and expectations as the Java PacingTest, so both sides define the statistics identically (src/IntervalStats.h).
#include "../src/IntervalStats.h"
#include <cmath>
#include <cstdio>

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
static bool Near(double a, double b) { return std::fabs(a - b) < 1e-9; }

int main()
{
    std::vector<double> v;
    for (int i = 100; i >= 1; --i) v.push_back(i);          // unsorted on purpose: 1..100 ms
    auto s = intervalstats::Summarize(v, 20);                  // period 20 ms: late > 30, very late > 60
    CHECK(s.samples == 100 && Near(s.meanMs, 50.5));
    CHECK(Near(s.p50Ms, 50) && Near(s.p95Ms, 95) && Near(s.p99Ms, 99) && Near(s.maxMs, 100));
    CHECK(Near(s.lateShare, 0.70) && Near(s.veryLateShare, 0.40));
    CHECK(intervalstats::Summarize({}, 20).samples == 0);
    auto one = intervalstats::Summarize({8.0}, 8.33);
    CHECK(one.samples == 1 && Near(one.p99Ms, 8.0) && Near(one.lateShare, 0));
    CHECK(Near(intervalstats::Summarize({5, 5, 5}, 0).lateShare, 0));                 // no period: no late shares
    CHECK(intervalstats::Row("Sleep(1)", s).find("p99  99.00") != std::string::npos);
    std::printf(g_fail == 0 ? "ALL OK (%d checks)\n" : "%d FAILURES\n", g_fail == 0 ? g_checks : g_fail);
    return g_fail == 0 ? 0 : 1;
}
