// IntervalStats.h - mean / percentiles / late share of measured intervals. Portable; the same definitions as agent/src/mcradar/PacingStats.java
// (nearest-rank percentiles, late = more than 1.5 periods, very late = more than 3), tested with the same data in tests/interval_stats_test.cpp.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace intervalstats
{
    struct Summary
    {
        size_t samples = 0;
        double meanMs = 0, p50Ms = 0, p95Ms = 0, p99Ms = 0, maxMs = 0, lateShare = 0, veryLateShare = 0;
    };

    inline double Percentile(const std::vector<double>& sorted, double q) // nearest rank
    {
        if (sorted.empty()) return 0;
        const long i = static_cast<long>(std::ceil(q * static_cast<double>(sorted.size()))) - 1;
        return sorted[static_cast<size_t>(std::clamp<long>(i, 0, static_cast<long>(sorted.size()) - 1))];
    }

    // `ms` are intervals in milliseconds; periodMs is the interval aimed for (0 = no late shares).
    inline Summary Summarize(std::vector<double> ms, double periodMs)
    {
        Summary s;
        s.samples = ms.size();
        if (ms.empty()) return s;
        std::sort(ms.begin(), ms.end());
        double sum = 0;
        size_t late = 0, very = 0;
        for (double v : ms)
        {
            sum += v;
            if (periodMs > 0 && v > periodMs * 1.5) ++late;
            if (periodMs > 0 && v > periodMs * 3) ++very;
        }
        s.meanMs = sum / static_cast<double>(ms.size());
        s.p50Ms = Percentile(ms, 0.50); s.p95Ms = Percentile(ms, 0.95); s.p99Ms = Percentile(ms, 0.99);
        s.maxMs = ms.back();
        s.lateShare = static_cast<double>(late) / static_cast<double>(ms.size());
        s.veryLateShare = static_cast<double>(very) / static_cast<double>(ms.size());
        return s;
    }

    inline std::string Row(const std::string& label, const Summary& s)
    {
        char buf[256];
        std::snprintf(buf, sizeof buf, "%-34s | mean %6.2f p50 %6.2f p95 %6.2f p99 %6.2f max %7.2f ms | late %5.1f%% very late %4.1f%%", label.c_str(), s.meanMs, s.p50Ms, s.p95Ms,
                      s.p99Ms, s.maxMs, s.lateShare * 100, s.veryLateShare * 100);
        return buf;
    }
} // namespace intervalstats
