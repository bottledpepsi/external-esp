// PerfStats.h - small portable counters and rolling windows for the overlay's own performance readouts (HUD, graphs, jitter meter).
//
//   CacheCounters / HitRatePct   how often the text-layout cache (Gfx) answered from memory over the last window
//
//   Ring<N>                      the last N values of something (frame time, data age); min / average / 95th percentile / max
//   JitterMeter                  the golden tests' jitter metric (tests/golden.h) on a live stream of on-screen positions
//   PerfHistory                  the three histories the graphs draw, the jitter meter, and the text under them (rebuilt four times a second)
//
// Nothing here allocates per frame; the overlay owns the instances and reads them a few times a second.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include "Types.h"
#include <string>

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

    struct RingStats
    {
        int n = 0;
        float min = 0, max = 0, avg = 0, p95 = 0;
    };

    template <int N>
    class Ring
    {
        static_assert(N >= 2 && N <= 4096, "a ring is a few hundred values");

    public:
        static constexpr int kCapacity = N;
        void Clear() { head_ = n_ = 0; }
        int Size() const { return n_; }
        // A value that is not a finite number is stored as 0 (a bad frame must not poison the scale); negative values as 0.
        void Push(float v)
        {
            if (!std::isfinite(v) || v < 0.f) v = 0.f;
            v_[head_] = v;
            head_ = (head_ + 1) % N;
            if (n_ < N) ++n_;
        }
        float At(int i) const { return v_[(head_ - n_ + i + 2 * N) % N]; } // 0 = the oldest, Size()-1 = the newest
        RingStats Summarize() const
        {
            RingStats s;
            s.n = n_;
            if (n_ == 0) return s;
            float tmp[N];
            double sum = 0;
            s.min = std::numeric_limits<float>::max();
            for (int i = 0; i < n_; ++i)
            {
                tmp[i] = At(i);
                sum += tmp[i];
                s.min = std::min(s.min, tmp[i]);
                s.max = std::max(s.max, tmp[i]);
            }
            s.avg = static_cast<float>(sum / n_);
            const int k = static_cast<int>(0.95 * static_cast<double>(n_ - 1)); // the same index rule as the golden metric
            std::nth_element(tmp, tmp + k, tmp + n_);
            s.p95 = tmp[k];
            return s;
        }

    private:
        float v_[N] = {};
        int head_ = 0, n_ = 0;
    };

    // The smallest of 4, 8, 16, 32, ... that is at least `v` - the top of a graph, so the scale does not jump about with every frame.
    inline float NiceCeiling(float v)
    {
        float c = 4.f;
        while (c < v && c < 1e6f) c *= 2.f;
        return c;
    }

    struct JitterStats
    {
        int n = 0; // frames that went into it
        double rms = 0, p95 = 0, max = 0; // pixels
    };

    // The golden tests' metric (tests/golden.h): for three consecutive frames the deviation of the middle one from the midpoint of its
    // neighbours, dev = | p_i - (p_(i-1) + p_(i+1)) / 2 |  - half the second difference of the on-screen position. Smooth motion gives about 0; a
    // box that stutters or vibrates gives a large value. A frame where the point was not visible (PushMissing) breaks the chain, as NaN does there.
    class JitterMeter
    {
    public:
        static constexpr int kWindow = 240;
        void Clear()
        {
            have_ = 0;
            dev_.Clear();
        }
        void Push(double x, double y)
        {
            if (!std::isfinite(x) || !std::isfinite(y)) return PushMissing();
            if (have_ == 3) // slide the three-point window
            {
                px_[0] = px_[1];
                py_[0] = py_[1];
                px_[1] = px_[2];
                py_[1] = py_[2];
                have_ = 2;
            }
            px_[have_] = x;
            py_[have_] = y;
            if (++have_ == 3) dev_.Push(static_cast<float>(std::hypot(px_[1] - (px_[0] + px_[2]) / 2, py_[1] - (py_[0] + py_[2]) / 2)));
        }
        void PushMissing() { have_ = 0; }
        JitterStats Get() const
        {
            JitterStats j;
            j.n = dev_.Size();
            if (j.n == 0) return j;
            double sq = 0;
            for (int i = 0; i < j.n; ++i) sq += static_cast<double>(dev_.At(i)) * dev_.At(i);
            j.rms = std::sqrt(sq / j.n);
            const RingStats r = dev_.Summarize();
            j.p95 = r.p95;
            j.max = r.max;
            return j;
        }

    private:
        double px_[3] = {}, py_[3] = {};
        int have_ = 0;
        Ring<kWindow> dev_;
    };

    // What the performance graphs show. The overlay pushes one value per frame while the graphs are on (and nothing while they are off) and calls
    // Refresh() four times a second: the texts under the graphs are rebuilt then, not per frame, so the text-layout cache keeps hitting.
    struct PerfHistory
    {
        static constexpr int kN = 240;
        Ring<kN> frameMs; // time between two frames (includes the wait for the display)
        Ring<kN> cpuMs; // what the overlay itself spent per frame
        Ring<kN> ageMs; // how old the newest agent sample was when the frame was drawn
        JitterMeter jitter; // the nearest player's box on the screen
        Ring<kN> wireKBps; // the agent's datagrams in KB per second, one value per Refresh()
        double wireBytesPerSec = 0, wireDgramsPerSec = 0, wireBytesPerDgram = 0; // the last window
        int entKind[kEntityKindCount] = {}; // entities drawn per category in the last frame (the overlay copies them in at Refresh time)
        int entSeen = 0;
        std::string frameText, ageText, jitterText, wireText, pipeText;

        // One reading of the datagram counters over `dtSec` seconds (the overlay calls it once per Refresh, with the deltas of StreamIngest's counters).
        void PushWire(double dtSec, uint64_t dBytes, uint64_t dDgrams)
        {
            if (!(dtSec > 0.0) || !std::isfinite(dtSec)) return;
            wireBytesPerSec = static_cast<double>(dBytes) / dtSec;
            wireDgramsPerSec = static_cast<double>(dDgrams) / dtSec;
            wireBytesPerDgram = dDgrams > 0 ? static_cast<double>(dBytes) / static_cast<double>(dDgrams) : 0.0;
            wireKBps.Push(static_cast<float>(wireBytesPerSec / 1024.0));
        }

        // The latency of what is on the screen, in pieces (ms, averages over the window): how old the newest sample was when the frame was drawn,
        // the wait between frames, what the overlay itself spent; and how often the agent sends. `total` = the first three.
        struct Pipeline
        {
            double ageMs = 0, frameMs = 0, cpuMs = 0, agentIntervalMs = 0, total = 0;
            bool valid = false;
        };
        Pipeline Breakdown() const
        {
            Pipeline p;
            const RingStats a = ageMs.Summarize(), f = frameMs.Summarize(), c = cpuMs.Summarize();
            if (a.n == 0 || f.n == 0) return p;
            p.ageMs = a.avg;
            p.frameMs = f.avg;
            p.cpuMs = c.avg;
            p.agentIntervalMs = wireDgramsPerSec > 0 ? 1000.0 / wireDgramsPerSec : 0.0;
            p.total = p.ageMs + p.frameMs + p.cpuMs;
            p.valid = true;
            return p;
        }

        void Clear()
        {
            frameMs.Clear();
            cpuMs.Clear();
            ageMs.Clear();
            jitter.Clear();
            frameText.clear();
            ageText.clear();
            jitterText.clear();
            wireText.clear();
            pipeText.clear();
            wireKBps.Clear();
            wireBytesPerSec = wireDgramsPerSec = wireBytesPerDgram = 0;
            for (int& n : entKind) n = 0;
            entSeen = 0;
        }

        void Refresh()
        {
            char b[160];
            if (const RingStats f = frameMs.Summarize(); f.n > 0)
            {
                const RingStats c = cpuMs.Summarize();
                std::snprintf(b, sizeof b, "frame %.1f ms avg  p95 %.1f  max %.1f  |  cpu %.2f ms", f.avg, f.p95, f.max, c.avg);
                frameText = b;
            }
            else frameText.clear();
            if (const RingStats a = ageMs.Summarize(); a.n > 0)
            {
                std::snprintf(b, sizeof b, "data age %.1f ms avg  p95 %.1f  max %.1f", a.avg, a.p95, a.max);
                ageText = b;
            }
            else ageText.clear();
            if (const JitterStats j = jitter.Get(); j.n >= 10)
            {
                std::snprintf(b, sizeof b, "jitter (nearest player) rms %.2f px  p95 %.2f  max %.2f", j.rms, j.p95, j.max);
                jitterText = b;
            }
            else jitterText = "jitter: no player in view";
            if (wireKBps.Size() > 0)
            {
                std::snprintf(b, sizeof b, "wire %.1f KB/s  |  %.1f datagrams/s  |  %.0f B each", wireBytesPerSec / 1024.0, wireDgramsPerSec, wireBytesPerDgram);
                wireText = b;
            }
            else wireText.clear();
            if (const Pipeline p = Breakdown(); p.valid)
            {
                std::snprintf(b, sizeof b, "latency %.1f = age %.1f + frame %.1f + cpu %.2f ms  (agent every %.0f ms)", p.total, p.ageMs, p.frameMs, p.cpuMs, p.agentIntervalMs);
                pipeText = b;
            }
            else pipeText.clear();
        }
    };
} // namespace perf
