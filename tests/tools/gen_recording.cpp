// gen_recording - writes a SYNTHETIC .esprec: what the agent would send for a modelled game session.
//
//   gen_recording [--seconds S] [--seed N] <out.esprec>
//
// NOT the original jitter_sim (that lives in jitter-fix.zip, which was not available when this was written): it is a fresh model of the same
// pipeline, written from its description, so numbers from the two are not comparable.
//
// Model (all times are exact; the only randomness is a seeded generator, so the same seed gives the same file):
//   game     the camera pose is a smooth function of time, sampled by render frames at ~144 Hz (+-0.3 ms); entities move on a smooth path
//            but the game only updates their position fields on 20 Hz ticks, so what a reader sees is a staircase.
//   agent    wakes every 8.33 ms (120 Hz) with scheduler jitter (sigma 0.35 ms, 2 % of the wake-ups 1-3 ms late), reads the last render
//            frame's camera and the last tick's entity positions, stamps the datagram with its own nanoTime (arbitrary epoch).
//   UDP      arrival = sample time + 0.12 ms + Exp(0.15 ms), 1 % of the datagrams delayed another 2-5 ms; order is preserved.
// Scene: the local player walks slowly toward -z; mouse look = slow sways plus two 55-degree flicks (peak ~550 deg/s), around 180 degrees so the
// reported yaw keeps wrapping through +-180; the sprint FOV widens 70 -> 77 over half a second and back. Bob runs in a circle ~28 m ahead,
// Carol stands still, Dave paces left and right across the view.
#include "../../src/EspRec.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

namespace
{
    struct Rng // SplitMix64: identical output everywhere (std::normal_distribution is not portable)
    {
        uint64_t s;
        explicit Rng(uint64_t seed) : s(seed) {}
        uint64_t Next()
        {
            uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            return z ^ (z >> 31);
        }
        double U() { return (static_cast<double>(Next() >> 11) + 0.5) / 9007199254740992.0; } // (0,1)
        double Normal() { return std::sqrt(-2 * std::log(U())) * std::cos(6.283185307179586 * U()); }
        double Exp(double mean) { return -mean * std::log(U()); }
    };

    constexpr double kPi = 3.14159265358979323846;
    double Smooth(double x) { x = std::clamp(x, 0.0, 1.0); return x * x * (3 - 2 * x); }

    struct Pose { double x, y, z, yaw, pitch, fov; };

    // ---- ground truth, continuous in time ----
    Pose Camera(double t)
    {
        Pose p{};
        p.x = 100 + 0.2 * t; // a slow walk toward -z (1.5 m/s) with a slight drift
        p.z = -50 - 1.5 * t;
        p.y = 64 + 1.62;
        // mouse look around 180 degrees (facing -z): slow sways, plus two flicks of 55 degrees (peak ~550 deg/s) at 6.0 s and 9.0 s
        p.yaw = 180 + 18 * std::sin(0.9 * t) + 12 * std::sin(0.37 * t + 1.0) + 55 * Smooth((t - 6.0) / 0.15) - 55 * Smooth((t - 9.0) / 0.2);
        p.pitch = 8 * std::sin(1.3 * t) + 4 * std::sin(0.45 * t);
        p.fov = 70 + 7 * Smooth((t - 3.0) / 0.5) - 7 * Smooth((t - 9.0) / 0.5); // sprint start / stop
        return p;
    }
    void Entity(int which, double t, double& x, double& z)
    {
        switch (which)
        {
        case 0: // Bob: circle, radius 5 m, 5.6 m/s, about 28 m ahead
            x = 104 + 5 * std::cos(t * 5.6 / 5);
            z = -80 + 5 * std::sin(t * 5.6 / 5);
            break;
        case 1: // Carol: standing still
            x = 96;
            z = -78;
            break;
        default: // Dave: pacing left and right across the view, up to 4 m/s
            x = 100 + 8 * std::sin(0.5 * t);
            z = -72;
            break;
        }
    }
} // namespace

int main(int argc, char** argv)
{
    double seconds = 12;
    uint64_t seed = 1;
    const char* out = nullptr;
    for (int i = 1; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 10);
        else out = argv[i];
    }
    if (!out || seconds <= 0)
    {
        std::fputs("usage: gen_recording [--seconds S] [--seed N] <out.esprec>\n", stderr);
        return 2;
    }
    std::ofstream f(out, std::ios::binary | std::ios::trunc);
    esprec::Writer w(f);
    esprec::Header h;
    h.wireVersion = 4;
    h.startUnixMs = 1790000000000LL; // fixed: the file must not depend on when it was generated
    w.WriteHeader(h);

    Rng rng(seed);
    const double P = 1.0 / 120, kFrame = 1.0 / 144, kTick = 0.05, kTickPhase = 0.013;
    const int64_t agentEpochNs = 123456789012LL;
    double tSample = 0, lastArrival = 0;
    static const char* const kNames[] = {"Bob", "Carol", "Dave"};
    for (int k = 0;; ++k)
    {
        double wake = k * P + 0.00035 * rng.Normal();
        if (rng.U() < 0.02) wake += 0.001 + 0.002 * rng.U();
        tSample = std::max(wake, tSample + 0.001);
        if (tSample > seconds) break;

        // what the game exposes to the agent at tSample
        const double frameT = std::floor(tSample / kFrame) * kFrame; // last render frame
        Pose cam = Camera(std::min(frameT, tSample));
        cam.yaw = std::remainder(cam.yaw, 360.0); // like the wire: the game reports a wrapped yaw, so the smoother's wrap handling is exercised
        const double tickT = std::floor((tSample - kTickPhase) / kTick) * kTick + kTickPhase;      // last entity tick
        const Pose me = Camera(tickT);

        char buf[1400];
        int n = std::snprintf(buf, sizeof buf, "MCRADAR 4\nT %lld\nS agent ok: 3 other player(s) [synthetic]\n",
                              static_cast<long long>(agentEpochNs + static_cast<int64_t>(tSample * 1e9)));
        n += std::snprintf(buf + n, sizeof buf - n, "C %.4f %.4f %.4f %.2f %.2f %.2f 1\n", cam.x, cam.y, cam.z, cam.yaw, cam.pitch, cam.fov);
        n += std::snprintf(buf + n, sizeof buf - n, "L Me %.3f %.3f %.3f %.2f %.2f 20.00 0.60 1.80 11111111-1111-1111-1111-111111111111\n", me.x, me.y - 1.62, me.z, me.yaw, me.pitch);
        for (int e = 0; e < 3; ++e)
        {
            double x, z;
            Entity(e, tickT, x, z);
            n += std::snprintf(buf + n, sizeof buf - n, "P %s %.3f 64.000 %.3f %.2f 0.00 20.00 0.60 1.80 2222222%d-2222-2222-2222-222222222222\n", kNames[e], x, z,
                               45.0 * e, e);
        }
        double arrival = tSample + 0.00012 + rng.Exp(0.00015);
        if (rng.U() < 0.01) arrival += 0.002 + 0.003 * rng.U();
        arrival = std::max(arrival, lastArrival + 1e-6); // loopback keeps the order
        lastArrival = arrival;
        w.Append(static_cast<uint64_t>(arrival * 1e9), buf, static_cast<size_t>(n));
    }
    f.flush();
    if (!f)
    {
        std::fprintf(stderr, "cannot write %s\n", out);
        return 1;
    }
    std::printf("wrote %s: %llu datagrams, %.1f s, seed %llu\n", out, static_cast<unsigned long long>(w.Records()), seconds, static_cast<unsigned long long>(seed));
    return 0;
}
