// PerfStats.h - small portable counters and rolling windows for the overlay's own performance readouts (HUD, graphs, jitter meter).
//
//   CacheCounters / HitRatePct   how often the text-layout cache (Gfx) answered from memory over the last window
//
// Nothing here allocates; the overlay owns the instances and reads them a few times a second.
#pragma once
#include <cstdint>

namespace perf
{
    struct CacheCounters
    {
        uint64_t hits = 0, misses = 0, clears = 0; // cumulative since the cache was created; `clears` = times it was emptied because it was full
    };

    // The hit rate (0..100) between two readings of the same counters; -1 when there was no lookup in between. A counter that went backwards
    // (the cache was recreated, e.g. after a device loss) counts as "since zero".
    inline float HitRatePct(const CacheCounters& prev, const CacheCounters& cur)
    {
        const bool reset = cur.hits < prev.hits || cur.misses < prev.misses;
        const uint64_t h = reset ? cur.hits : cur.hits - prev.hits;
        const uint64_t m = reset ? cur.misses : cur.misses - prev.misses;
        const uint64_t n = h + m;
        return n == 0 ? -1.f : 100.f * static_cast<float>(h) / static_cast<float>(n);
    }
} // namespace perf
