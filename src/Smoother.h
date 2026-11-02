// Smoother.h - turns the agent's discrete samples (up to 240/s, uneven spacing) into a pose for every rendered
// frame (260+ fps), so the boxes follow the game's own picture instead of shaking around it.
//
// What the game actually shows, and what this reproduces:
//  * The CAMERA (and the local player's yaw/pitch) change every rendered frame. Between two samples the pose is
//    extrapolated with the velocity of the last two samples. Extrapolation is wrong by a little at every new
//    sample; instead of letting the display jump to the new value, the difference is kept as a "visual
//    correction" that decays within ~30 ms (error blending), so there is no per-sample snap.
//  * ENTITY positions (other players, the local player's feet) are updated once per game tick (20 Hz). The game
//    does not draw them there: it interpolates from the previous tick's value to the current one over the tick
//    (partialTick). We detect the tick steps in the samples and do the same interpolation. Extrapolating tick
//    steps by velocity (what an earlier version did) alternates between 0 and 3x the true speed - that is the
//    "boxes vibrate around a moving player" effect.
//  * Time base: the agent stamps every sample with the instant it was read (its System.nanoTime). Mapping that
//    onto our clock with a windowed minimum of (arrival - stamp) removes UDP / thread-wake-up jitter and the
//    delay of slow agent work between reading and sending. Samples without a stamp (older agents, demo) fall
//    back to their arrival time.
// Yaw is unwrapped (359 -> 0 does not spin). Purely cosmetic; can be turned off (then the latest raw sample is used).
#pragma once
#include "Types.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

// The numbers that shape the smoothing. Defaults are the values the golden recordings were tuned with; the ADVANCED tab lets the user change them
// while the overlay runs. Every field is in seconds. Sanitized() clamps to the ranges the UI offers, so a hand-edited config cannot produce a
// division by zero or an extrapolation into the next minute.
#ifndef SMOOTHER_BASELINE
#define SMOOTHER_BASELINE 0.012
#endif
#ifndef SMOOTHER_BLEND_TAU
#define SMOOTHER_BLEND_TAU 0.010
#endif
struct SmootherTuning
{
    // Velocity is measured against a sample at least this old. The game redraws every ~4 ms, so two samples a few ms apart often hit the same
    // frame (delta 0) or two (delta 2x): too noisy to extrapolate from.
    double baseline = SMOOTHER_BASELINE;
    double blendTau = SMOOTHER_BLEND_TAU; // decay time of the visual correction after a new sample (s); 0 = off (the display snaps)
    double maxAhead = 0.05; // never extrapolate further than this past the newest sample (s); 0 = no extrapolation
    double tick = 0.05; // the game tick that entity positions are interpolated over (s); 0.05 = 20 TPS

    static constexpr double kBaselineMin = 0.002, kBaselineMax = 0.050;
    static constexpr double kBlendTauMax = 0.050;
    static constexpr double kMaxAheadMax = 0.100;
    static constexpr double kTickMin = 0.025, kTickMax = 0.100;

    SmootherTuning Sanitized() const
    {
        auto fin = [](double v, double fallback) { return std::isfinite(v) ? v : fallback; };
        const SmootherTuning d;
        SmootherTuning t;
        t.baseline = std::clamp(fin(baseline, d.baseline), kBaselineMin, kBaselineMax);
        t.blendTau = std::clamp(fin(blendTau, d.blendTau), 0.0, kBlendTauMax);
        t.maxAhead = std::clamp(fin(maxAhead, d.maxAhead), 0.0, kMaxAheadMax);
        t.tick = std::clamp(fin(tick, d.tick), kTickMin, kTickMax);
        return t;
    }
    bool operator==(const SmootherTuning& o) const { return baseline == o.baseline && blendTau == o.blendTau && maxAhead == o.maxAhead && tick == o.tick; }
};

// How wrong the extrapolation was at each new camera sample, smoothed (an exponential average over the last ~20 samples). This is the size of the
// "visual correction" the blend has to hide: the live readout in the ADVANCED tab, in degrees / blocks. Zero until two samples have arrived.
struct SmootherStats
{
    double camAngErrDeg = 0; // hypot(yaw error, pitch error)
    double camPosErr = 0; // blocks
    uint64_t samples = 0;
    // For the HUD (both averaged, in ms): how old the newest sample is at the moment a frame is drawn (the age of what you see, excluding the
    // loopback transport, which is microseconds), and how much later than the best case a sample arrives (arrival - stamp - the smallest such
    // difference of the last ~5 s; clock offset cancels out). 0 until measured.
    double ageMs = 0;
    double jitterMs = 0;
};

class Smoother
{
public:
    using Clock = std::chrono::steady_clock;

    void SetTuning(const SmootherTuning& t) { tune_ = t.Sanitized(); }
    const SmootherTuning& Tuning() const { return tune_; }
    const SmootherStats& Stats() const { return stats_; }

    // Call whenever a snapshot with a new seq arrives.
    void Push(const WorldSnapshot& s)
    {
        if (!haveCur_) epoch_ = s.arrived;
        double ts = SampleTime(s);
        if (haveCur_ && ts <= lastTs_ + 1e-4) ts = lastTs_ + 1e-4; // strictly increasing
        lastTs_ = ts;

        // v4 datagrams without an item list keep the previous one
        const bool keepItems = !s.hasItems && haveCur_;
        std::vector<ItemState> keep;
        if (keepItems) keep = std::move(cur_.items);
        cur_ = s;
        if (keepItems) cur_.items = std::move(keep);
        cur_.hasItems = true;
        haveCur_ = true;

        // ---- camera (continuous) ----
        if (s.camera.valid)
        {
            const CameraState& c = s.camera;
            Pose p;
            p.t = ts;
            p.mode = c.mode;
            p.fovOk = c.fovValid;
            p.v[0] = c.pos.x; p.v[1] = c.pos.y; p.v[2] = c.pos.z;
            p.v[3] = c.yawDeg; p.v[4] = c.pitchDeg; p.v[5] = c.fovDeg;
            if (cam_.n > 0 && cam_.p[0].mode != p.mode) cam_.Reset(); // first/third person switch = a cut
            cam_.Add(p);
        }
        else cam_.Reset();

        // ---- local player: yaw/pitch continuous, feet position tick-stepped ----
        if (s.localValid)
        {
            if (s.local.yawValid && s.local.pitchValid)
            {
                Pose p;
                p.t = ts;
                p.v[3] = s.local.yawDeg;
                p.v[4] = s.local.pitchDeg;
                loc_.Add(p);
            }
            else loc_.Reset();
            if (!haveLocalTrack_)
            {
                localTrack_.prev = localTrack_.cur = Mot(s.local);
                localTrack_.tCur = ts;
                haveLocalTrack_ = true;
            }
            else UpdateTrack(localTrack_, Mot(s.local), ts);
        }
        else
        {
            loc_.Reset();
            haveLocalTrack_ = false;
        }

        // ---- other players: tick-stepped ----
        ++gen_;
        for (const auto& p : s.players)
        {
            auto [it, inserted] = tracks_.try_emplace(p.name);
            Track& t = it->second;
            if (inserted)
            {
                t.prev = t.cur = Mot(p);
                t.tCur = ts;
            }
            else UpdateTrack(t, Mot(p), ts);
            t.gen = gen_;
        }
        for (auto it = tracks_.begin(); it != tracks_.end();)
            it = it->second.gen == gen_ ? std::next(it) : tracks_.erase(it);
    }

    bool HasData() const { return haveCur_; }

    WorldSnapshot Predict(Clock::time_point now, bool enable)
    {
        WorldSnapshot out;
        PredictInto(now, enable, out);
        return out;
    }

    // Same as Predict(), but writes into a caller-owned snapshot. Copy-assignment reuses the vectors' and strings'
    // existing capacity, so in steady state this allocates nothing.
    void PredictInto(Clock::time_point now, bool enable, WorldSnapshot& out)
    {
        out = cur_;
        if (!enable || !haveCur_) return;
        const double tn = Sec(now);
        if (lastSampleT_ > 0) // the age of the newest sample, averaged over ~50 frames
        {
            const double age = std::max(0.0, (tn - lastSampleT_) * 1e3);
            ageEma_ = ageN_++ == 0 ? age : ageEma_ + 0.02 * (age - ageEma_);
            stats_.ageMs = ageEma_;
        }

        // entity positions: the game's own tick interpolation
        if (haveLocalTrack_) ApplyTrack(localTrack_, tn, out.local);
        for (auto& pl : out.players)
        {
            const auto it = tracks_.find(pl.name);
            if (it != tracks_.end()) ApplyTrack(it->second, tn, pl);
        }

        // camera and head turns: extrapolation with error blending
        double v[6];
        if (cam_.n > 0 && out.camera.valid)
        {
            Eval(cam_, tn, v, true);
            CameraState& c = out.camera;
            c.pos = {v[0], v[1], v[2]};
            c.yawDeg = static_cast<float>(v[3]);
            c.pitchDeg = static_cast<float>(v[4]);
            if (c.fovValid) c.fovDeg = static_cast<float>(v[5]);
        }
        if (loc_.n > 0 && out.localValid && out.local.yawValid && out.local.pitchValid)
        {
            Eval(loc_, tn, v, false);
            out.local.yawDeg = static_cast<float>(v[3]);
            out.local.pitchDeg = static_cast<float>(v[4]);
        }
    }

private:
    static constexpr int kHist = 8;
#ifndef SMOOTHER_BLEND_TAU
#define SMOOTHER_BLEND_TAU 0.010
#endif

    struct Pose
    {
        double t = 0; // sample instant, seconds since epoch_
        double v[6] = {0, 0, 0, 0, 0, 0}; // x y z yaw pitch fov
        bool fovOk = false;
        int mode = 1;
    };

    // The last three samples of one continuous quantity, plus the visual correction that hides prediction errors.
    struct Cont
    {
        Pose p[kHist]; // newest first
        int n = 0;
        bool dirty = false, modelValid = false, mBValid = false;
        Pose mA, mB; // the pair the previous evaluation extrapolated from
        double off[6] = {0, 0, 0, 0, 0, 0}; // correction, valid at offAt, decays to 0
        double offAt = 0;

        void Reset()
        {
            n = 0;
            dirty = modelValid = mBValid = false;
            for (double& o : off) o = 0;
        }

        void Add(const Pose& s)
        {
            for (int i = kHist - 1; i > 0; --i) p[i] = p[i - 1];
            p[0] = s;
            n = std::min(n + 1, kHist);
            dirty = true;
        }

        // index of the sample the velocity is measured against: the first one >= `baseline` older than the newest
        // (else the oldest usable one); -1 when there is none (single sample, a view-mode cut, a long gap)
        int Base(double baseline) const
        {
            int best = -1;
            for (int i = 1; i < n; ++i)
            {
                const double dt = p[0].t - p[i].t;
                if (p[i].mode != p[0].mode || dt > 0.25) break;
                best = i;
                if (dt >= baseline) break;
            }
            return best;
        }
    };

    // Only the fields that move; names, uuids and health come from the latest snapshot (no string copies per frame).
    struct Motion
    {
        Vec3 pos;
        float yawDeg = 0, pitchDeg = 0;
        bool yawValid = false, pitchValid = false;
    };

    struct Track
    {
        Motion prev, cur; // the display moves prev -> cur over one tick, starting when cur was first seen
        double tCur = 0;
        uint64_t gen = 0;
    };

    static Motion Mot(const PlayerState& p) { return Motion{p.pos, p.yawDeg, p.pitchDeg, p.yawValid, p.pitchValid}; }

    static double Wrap(double d)
    {
        while (d > 180) d -= 360;
        while (d < -180) d += 360;
        return d;
    } // shortest angular delta

    double Sec(Clock::time_point tp) const { return std::chrono::duration<double>(tp - epoch_).count(); }

    // ---- time base -------------------------------------------------------------------------------------------
    // sample instant on our clock = agent stamp + min(arrival - stamp) over the last ~5 s: the minimum is the
    // constant clock offset plus the *smallest* transport delay; everything above it is jitter.
    double SampleTime(const WorldSnapshot& s)
    {
        const double arrived = Sec(s.arrived);
        if (!s.hasSampleTime)
        {
            lastSampleT_ = arrived; // an agent without stamps: the age is counted from the arrival
            return arrived;
        }
        const auto arrivedNs = static_cast<int64_t>(arrived * 1e9);
        const int64_t delta = arrivedNs - s.sampleNs;
        const int64_t bucket = arrivedNs / 1000000000LL;
        if (!buckets_.empty() && std::llabs(delta - minDelta_) > 2000000000LL) buckets_.clear(); // clock jump / restart
        while (!buckets_.empty() && buckets_.front().first < bucket - 4) buckets_.erase(buckets_.begin());
        if (buckets_.empty() || buckets_.back().first != bucket) buckets_.emplace_back(bucket, delta);
        else buckets_.back().second = std::min(buckets_.back().second, delta);
        int64_t m = buckets_.front().second;
        for (const auto& b : buckets_) m = std::min(m, b.second);
        minDelta_ = m;
        jitterEma_ = jitterN_++ == 0 ? 0.0 : jitterEma_ + 0.05 * (static_cast<double>(delta - m) * 1e-6 - jitterEma_);
        stats_.jitterMs = jitterEma_;
        lastSampleT_ = static_cast<double>(s.sampleNs + m) * 1e-9;
        return lastSampleT_;
    }

    // ---- continuous quantities ---------------------------------------------------------------------------------
    void Predict(const Pose& a, const Pose* b, double tn, double* out) const
    {
        for (int i = 0; i < 6; ++i) out[i] = a.v[i];
        if (!b) return;
        const double dt = a.t - b->t;
        if (dt < 8e-4 || dt > 0.25 || a.mode != b->mode) return; // too close / stale / a cut: no extrapolation
        const double h = std::clamp(tn - a.t, 0.0, std::min(2.0 * dt, tune_.maxAhead));
        const double f = h / dt;
        for (int i = 0; i < 6; ++i)
        {
            double d = a.v[i] - b->v[i];
            if (i == 3) d = Wrap(d);
            if (i == 5 && !(a.fovOk && b->fovOk)) d = 0;
            out[i] = a.v[i] + d * f;
        }
    }

    void Eval(Cont& c, double tn, double* out, bool isCamera)
    {
        double now[6];
        const int bi = c.Base(tune_.baseline);
        Predict(c.p[0], bi >= 0 ? &c.p[bi] : nullptr, tn, now);
        if (c.dirty)
        {
            if (c.modelValid)
            {
                // Where the display was heading under the previous samples (with its decaying correction): the new
                // model must start from there, otherwise the picture jumps by the prediction error.
                double old[6];
                Predict(c.mA, c.mBValid ? &c.mB : nullptr, tn, old);
                const double k = tune_.blendTau > 0 ? std::exp(-(tn - c.offAt) / tune_.blendTau) : 0.0;
                bool cut = false;
                for (int i = 0; i < 6; ++i)
                {
                    double d = old[i] + c.off[i] * k - now[i];
                    if (i == 3) d = Wrap(d);
                    c.off[i] = d;
                }
                // a big disagreement is a teleport or a view change, not an error to blend away
                if (std::fabs(c.off[0]) > 2 || std::fabs(c.off[1]) > 2 || std::fabs(c.off[2]) > 2 ||
                    std::fabs(c.off[3]) > 30 || std::fabs(c.off[4]) > 30) cut = true;
                if (isCamera && !cut) RecordError(std::hypot(c.off[3], c.off[4]), std::sqrt(c.off[0] * c.off[0] + c.off[1] * c.off[1] + c.off[2] * c.off[2]));
                if (cut) for (double& o : c.off) o = 0;
                c.offAt = tn;
            }
            c.dirty = false;
        }
        const double k = tune_.blendTau > 0 ? std::exp(-(tn - c.offAt) / tune_.blendTau) : 0.0;
        for (int i = 0; i < 6; ++i) out[i] = now[i] + c.off[i] * k;
        c.mA = c.p[0];
        if (bi >= 0) c.mB = c.p[bi];
        c.mBValid = bi >= 0;
        c.modelValid = true;
    }

    // ---- tick-stepped entities ---------------------------------------------------------------------------------
    static bool Changed(const Motion& a, const Motion& b)
    {
        if (std::fabs(a.pos.x - b.pos.x) + std::fabs(a.pos.y - b.pos.y) + std::fabs(a.pos.z - b.pos.z) > 1e-4) return true;
        if (a.yawValid != b.yawValid || a.pitchValid != b.pitchValid) return true;
        if (a.yawValid && std::fabs(Wrap(a.yawDeg - b.yawDeg)) > 1e-3) return true;
        if (a.pitchValid && std::fabs(a.pitchDeg - b.pitchDeg) > 1e-3) return true;
        return false;
    }

    double Alpha(double t, double tCur) const { return std::clamp((t - tCur) / tune_.tick, 0.0, 1.0); }

    void RecordError(double angDeg, double pos)
    {
        constexpr double a = 0.1;
        stats_.camAngErrDeg = stats_.samples ? stats_.camAngErrDeg + a * (angDeg - stats_.camAngErrDeg) : angDeg;
        stats_.camPosErr = stats_.samples ? stats_.camPosErr + a * (pos - stats_.camPosErr) : pos;
        ++stats_.samples;
    }

    static Motion Lerp(const Motion& prev, const Motion& cur, double a)
    {
        Motion r = cur;
        r.pos = {prev.pos.x + (cur.pos.x - prev.pos.x) * a, prev.pos.y + (cur.pos.y - prev.pos.y) * a,
                 prev.pos.z + (cur.pos.z - prev.pos.z) * a};
        if (prev.yawValid && cur.yawValid)
            r.yawDeg = static_cast<float>(prev.yawDeg + Wrap(cur.yawDeg - prev.yawDeg) * a);
        if (prev.pitchValid && cur.pitchValid)
            r.pitchDeg = static_cast<float>(prev.pitchDeg + (cur.pitchDeg - prev.pitchDeg) * a);
        return r;
    }

    void UpdateTrack(Track& t, const Motion& m, double ts) const
    {
        if (Changed(t.cur, m))
        {
            Motion shown = Lerp(t.prev, t.cur, Alpha(ts, t.tCur)); // where the display is right now
            const double dx = shown.pos.x - m.pos.x, dy = shown.pos.y - m.pos.y, dz = shown.pos.z - m.pos.z;
            if (dx * dx + dy * dy + dz * dz > 64.0) shown = m; // teleport / respawn / dimension change: no glide
            t.prev = shown;
            t.cur = m;
            t.tCur = ts;
        }
        else t.cur = m; // same pose
    }

    void ApplyTrack(const Track& t, double tn, PlayerState& dst) const
    {
        const Motion l = Lerp(t.prev, t.cur, Alpha(tn, t.tCur));
        dst.pos = l.pos;
        if (dst.yawValid && l.yawValid) dst.yawDeg = l.yawDeg;
        if (dst.pitchValid && l.pitchValid) dst.pitchDeg = l.pitchDeg;
    }

    SmootherTuning tune_;
    SmootherStats stats_;
    double jitterEma_ = 0, ageEma_ = 0, lastSampleT_ = 0;
    uint64_t jitterN_ = 0, ageN_ = 0;
    WorldSnapshot cur_;
    bool haveCur_ = false;
    Clock::time_point epoch_{};
    double lastTs_ = 0;
    std::vector<std::pair<int64_t, int64_t>> buckets_; // (second, min arrival-stamp delta)
    int64_t minDelta_ = 0;

    Cont cam_, loc_;
    Track localTrack_;
    bool haveLocalTrack_ = false;
    std::unordered_map<std::string, Track> tracks_;
    uint64_t gen_ = 0;
};
