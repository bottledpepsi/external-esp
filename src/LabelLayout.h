// LabelLayout.h - where a text label goes so that labels do not sit on top of each other, and how far it has faded.
//
//   Fade(dist, maxDist, fadePct)   1 up to (100 - fadePct)% of the label distance, then a straight line down to 0 at the label distance.
//                                  An unlimited label distance never fades. fadePct 0 = the old hard cut at maxDist.
//   Placer                         a greedy pass: labels are offered nearest first; each one that overlaps something already placed is
//                                  moved by `step` pixels in its own direction (names up, distances down) until it is free, and is dropped
//                                  when it would have to move further than `maxSteps` steps. Fixed capacity: nothing is allocated, and a
//                                  frame with more labels than the capacity keeps the first ones (the nearest) and drops the rest.
//
// Portable (no Windows headers); the renderer owns one Placer and calls Reset() once per frame.
#pragma once
#include <algorithm>
#include <cmath>

namespace labels
{
    struct Rect
    {
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        float Width() const { return x1 - x0; }
        float Height() const { return y1 - y0; }
    };

    // Strictly overlapping: rectangles that only touch at an edge do not count.
    inline bool Overlap(const Rect& a, const Rect& b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }

    // A label distance above 1e30 is "no limit" (presets::kUnlimited).
    inline float Fade(float dist, float maxDist, int fadePct)
    {
        if (!(dist >= 0.f)) return 1.f; // NaN or negative: draw it
        if (maxDist > 1e30f) return 1.f; // unlimited
        if (dist > maxDist) return 0.f;
        const float pct = static_cast<float>(std::clamp(fadePct, 0, 100));
        if (pct <= 0.f) return 1.f;
        const float start = maxDist * (1.f - pct / 100.f);
        if (dist <= start) return 1.f;
        const float span = maxDist - start;
        return span > 0.f ? std::clamp(1.f - (dist - start) / span, 0.f, 1.f) : 1.f;
    }

    class Placer
    {
    public:
        static constexpr int kCapacity = 512; // 256 players x (name + distance) is the worst case a wire packet can hold

        void Reset()
        {
            n_ = 0;
            dropped_ = 0;
        }
        int Count() const { return n_; }
        int Dropped() const { return dropped_; } // labels of this frame that found no free place (cleared by Reset)

        // Tries `r` where it is, then moved by dir*step, dir*2*step ... dir*maxSteps*step (dir = -1 up, +1 down; screen y grows downward).
        // On success `r` holds the final rectangle, which is now taken. On failure `r` is unchanged and nothing is taken.
        bool Place(Rect& r, int dir, float step, int maxSteps)
        {
            if (n_ >= kCapacity)
            {
                ++dropped_;
                return false;
            }
            const float h = r.Height();
            for (int i = 0; i <= maxSteps; ++i)
            {
                Rect c = r;
                const float dy = static_cast<float>(dir) * step * static_cast<float>(i);
                c.y0 += dy;
                c.y1 = c.y0 + h;
                if (!Hits(c))
                {
                    r = c;
                    placed_[n_++] = c;
                    return true;
                }
            }
            ++dropped_;
            return false;
        }

        // Takes space without testing (declutter off, or a label that must be drawn): later labels move around it.
        void Occupy(const Rect& r)
        {
            if (n_ < kCapacity) placed_[n_++] = r;
        }

    private:
        bool Hits(const Rect& c) const
        {
            for (int i = 0; i < n_; ++i)
                if (Overlap(c, placed_[i])) return true;
            return false;
        }

        Rect placed_[kCapacity];
        int n_ = 0;
        int dropped_ = 0;
    };
} // namespace labels
