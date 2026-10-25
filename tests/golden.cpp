#include "golden.h"
#include "Camera.h"
#include "Json.h"
#include "Replay.h"
#include "Smoother.h"
#include "StreamIngest.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace golden
{
    namespace
    {
        using Clock = StreamIngest::Clock;
        struct Pt { double x = std::numeric_limits<double>::quiet_NaN(), y = std::numeric_limits<double>::quiet_NaN(); };

        Metrics Compute(const std::vector<Pt>& p, size_t firstMeasured);
    } // namespace

    Metrics Jitter(const std::vector<std::pair<double, double>>& px, size_t firstMeasured)
    {
        std::vector<Pt> p;
        for (const auto& v : px) p.push_back(Pt{v.first, v.second});
        return Compute(p, firstMeasured);
    }

    namespace
    {
        Metrics Compute(const std::vector<Pt>& p, size_t firstMeasured)
        {
            Metrics m;
            std::vector<double> dev;
            for (size_t i = std::max<size_t>(firstMeasured, 1); i + 1 < p.size(); ++i)
            {
                if (std::isnan(p[i - 1].x) || std::isnan(p[i].x) || std::isnan(p[i + 1].x)) continue;
                const double mx = (p[i - 1].x + p[i + 1].x) / 2, my = (p[i - 1].y + p[i + 1].y) / 2;
                dev.push_back(std::hypot(p[i].x - mx, p[i].y - my));
                m.maxStepPx = std::max(m.maxStepPx, std::hypot(p[i].x - p[i - 1].x, p[i].y - p[i - 1].y));
            }
            m.frames = static_cast<double>(dev.size());
            if (dev.empty()) return m;
            double sq = 0;
            for (double d : dev) sq += d * d;
            m.jitterRmsPx = std::sqrt(sq / static_cast<double>(dev.size()));
            std::sort(dev.begin(), dev.end());
            m.jitterP95Px = dev[static_cast<size_t>(0.95 * static_cast<double>(dev.size() - 1))];
            m.jitterMaxPx = dev.back();
            return m;
        }
    } // namespace

    bool Measure(const std::string& path, const Options& opt, Result& out, std::string* error)
    {
        ReplayPlayer player;
        if (!player.Load(path, error)) return false;
        const Clock::time_point t0 = Clock::time_point(std::chrono::seconds(1000000));
        Clock::time_point now = t0;
        StreamIngest ingest([&] { return now; });
        Smoother smoother;
        uint64_t lastSeq = 0;
        WorldSnapshot snap;

        const double dt = 1.0 / opt.frameHz;
        const double durationSec = static_cast<double>(player.DurationNs()) / 1e9 + 0.25;
        const size_t frames = static_cast<size_t>(durationSec * opt.frameHz);
        const size_t warm = static_cast<size_t>(opt.warmupSec * opt.frameHz);
        std::map<std::string, std::vector<Pt>> tracks;
        bool haveAnchor = false;
        Vec3 anchor;

        for (size_t k = 0; k < frames; ++k)
        {
            now = t0 + std::chrono::nanoseconds(static_cast<int64_t>(static_cast<double>(k) * dt * 1e9));
            player.Pump(ingest, now);
            if (auto sp = ingest.Latest(); sp && sp->seq != lastSeq)
            {
                lastSeq = sp->seq;
                smoother.Push(*sp);
            }
            if (smoother.HasData()) smoother.PredictInto(now, opt.smoothing != 0, snap);
            else continue;

            Camera cam;
            bool haveCam = false;
            if (snap.camera.valid)
            {
                cam.eye = snap.camera.pos;
                cam.yawDeg = snap.camera.yawDeg;
                cam.pitchDeg = snap.camera.pitchDeg;
                cam.fovYDeg = snap.camera.fovValid ? snap.camera.fovDeg : 70;
                haveCam = true;
            }
            else if (snap.localValid && snap.local.yawValid && snap.local.pitchValid)
            {
                cam.eye = {snap.local.pos.x, snap.local.pos.y + Camera::kEyeHeight, snap.local.pos.z};
                cam.yawDeg = snap.local.yawDeg;
                cam.pitchDeg = snap.local.pitchDeg;
                cam.fovYDeg = 70;
                haveCam = true;
            }
            if (!haveCam) continue;
            cam.w = opt.width;
            cam.h = opt.height;
            cam.Update();
            if (!haveAnchor)
            {
                haveAnchor = true;
                anchor = {cam.eye.x + cam.f.x * 8, cam.eye.y + cam.f.y * 8, cam.eye.z + cam.f.z * 8};
            }
            auto record = [&](const std::string& name, const Vec3& world)
            {
                auto& tr = tracks[name];
                if (tr.size() < frames) tr.resize(frames);
                float sx, sy;
                if (cam.Project(world, sx, sy)) tr[k] = Pt{sx, sy};
            };
            record("world_anchor", anchor);
            for (const auto& p : snap.players) record("player:" + p.name, Vec3{p.pos.x, p.pos.y + 0.9, p.pos.z});
        }
        out.clear();
        for (const auto& [name, tr] : tracks)
        {
            Metrics m = Compute(tr, warm);
            if (m.frames > 0) out[name] = m;
        }
        if (out.empty())
        {
            if (error) *error = "no probe produced any measurable frame (no camera, or everything behind it)";
            return false;
        }
        return true;
    }

    std::string ToJson(const std::string& recordingName, const Options& opt, const Result& r)
    {
        // json::Escape() returns the quoted string
        std::string s = "{\n  " + json::Escape("format") + ": 1,\n  " + json::Escape("recording") + ": " + json::Escape(recordingName) + ",\n";
        auto num = [&](const std::string& k, double v) { s += "  " + json::Escape(k) + ": " + json::Number(v) + ",\n"; };
        num("frame_hz", opt.frameHz);
        num("width", opt.width);
        num("height", opt.height);
        num("warmup_s", opt.warmupSec);
        std::vector<std::string> lines;
        char b[64];
        auto px = [&](const std::string& key, double v)
        {
            std::snprintf(b, sizeof b, "%.4f", v);
            lines.push_back("  " + json::Escape(key) + ": " + b);
        };
        for (const auto& [name, m] : r)
        {
            lines.push_back("  " + json::Escape("probe/" + name + "/frames") + ": " + json::Number(m.frames));
            px("probe/" + name + "/jitter_rms_px", m.jitterRmsPx);
            px("probe/" + name + "/jitter_p95_px", m.jitterP95Px);
            px("probe/" + name + "/jitter_max_px", m.jitterMaxPx);
            px("probe/" + name + "/max_step_px", m.maxStepPx);
        }
        for (size_t i = 0; i < lines.size(); ++i) s += lines[i] + (i + 1 < lines.size() ? ",\n" : "\n");
        s += "}\n";
        return s;
    }

    bool FromJson(const std::string& text, Options& opt, Result& r, std::string* error)
    {
        auto obj = json::ParseObject(text, error);
        if (!obj) return false;
        if (json::GetNumber(*obj, "format", 0) != 1)
        {
            if (error) *error = "golden file has an unknown \"format\"";
            return false;
        }
        opt.frameHz = json::GetNumber(*obj, "frame_hz", opt.frameHz);
        opt.width = static_cast<int>(json::GetNumber(*obj, "width", opt.width));
        opt.height = static_cast<int>(json::GetNumber(*obj, "height", opt.height));
        opt.warmupSec = json::GetNumber(*obj, "warmup_s", opt.warmupSec);
        r.clear();
        for (const auto& [key, val] : *obj)
        {
            if (key.rfind("probe/", 0) != 0) continue;
            const size_t slash = key.rfind('/');
            const std::string name = key.substr(6, slash - 6), metric = key.substr(slash + 1);
            Metrics& m = r[name];
            if (metric == "frames") m.frames = val.n;
            else if (metric == "jitter_rms_px") m.jitterRmsPx = val.n;
            else if (metric == "jitter_p95_px") m.jitterP95Px = val.n;
            else if (metric == "jitter_max_px") m.jitterMaxPx = val.n;
            else if (metric == "max_step_px") m.maxStepPx = val.n;
        }
        return true;
    }

    std::string Compare(const Result& golden, const Result& measured, const Tolerance& tol, std::string* notes)
    {
        std::string fail;
        char b[256];
        for (const auto& [name, g] : golden)
        {
            auto it = measured.find(name);
            if (it == measured.end())
            {
                fail += "probe '" + name + "' is in the golden file but was not measured\n";
                continue;
            }
            const Metrics& m = it->second;
            auto check = [&](const char* what, double got, double want, double slack)
            {
                const double limit = want * (1 + tol.relative) + slack;
                if (got > limit)
                {
                    std::snprintf(b, sizeof b, "%s %s: %.4f px > allowed %.4f px (golden %.4f, +%.0f%% +%.2f px)\n", name.c_str(), what, got, limit, want,
                                  tol.relative * 100, slack);
                    fail += b;
                }
                else if (notes && want > 0.5 && got < want * 0.5)
                {
                    std::snprintf(b, sizeof b, "%s %s improved: %.4f px vs golden %.4f px - consider tests/regen-goldens.sh\n", name.c_str(), what, got, want);
                    *notes += b;
                }
            };
            check("jitter_rms", m.jitterRmsPx, g.jitterRmsPx, tol.absolutePx);
            check("jitter_p95", m.jitterP95Px, g.jitterP95Px, tol.absolutePx);
            check("jitter_max", m.jitterMaxPx, g.jitterMaxPx, tol.absolutePx * 4);
            check("max_step", m.maxStepPx, g.maxStepPx, tol.stepAbsolutePx);
            if (std::fabs(m.frames - g.frames) > g.frames * tol.frameRelative + 1)
            {
                std::snprintf(b, sizeof b, "%s measured %.0f frames, golden has %.0f (the pipeline timing changed?)\n", name.c_str(), m.frames, g.frames);
                fail += b;
            }
        }
        for (const auto& [name, m] : measured)
            if (!golden.count(name)) fail += "probe '" + name + "' was measured but is not in the golden file (regenerate on purpose)\n";
        return fail;
    }
} // namespace golden
