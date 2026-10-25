// Proves the golden test can fail: the shipped recording measured with motion smoothing OFF (the raw-latest-sample path) must be clearly
// worse than the golden numbers, and golden::Compare must report it. If this test ever passes the "regression" it means the metric or the
// tolerances became too loose to catch anything.
//   golden_sensitivity <recording.esprec> <golden.json>
#include "golden.h"
#include <cstdio>
#include <fstream>
#include <iterator>

int main(int argc, char** argv)
{
    if (argc != 3) { std::fputs("usage: golden_sensitivity <recording.esprec> <golden.json>\n", stderr); return 2; }
    std::ifstream gf(argv[2]);
    const std::string text((std::istreambuf_iterator<char>(gf)), std::istreambuf_iterator<char>());
    golden::Options opt;
    golden::Result gold, on, off;
    std::string err;
    if (!golden::FromJson(text, opt, gold, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 2; }
    golden::Options offOpt = opt;
    offOpt.smoothing = 0;
    if (!golden::Measure(argv[1], opt, on, &err) || !golden::Measure(argv[1], offOpt, off, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 2; }
    int fails = 0;
    for (const auto& [name, g] : gold)
    {
        const auto& m = off[name];
        const double ratio = m.jitterRmsPx / (g.jitterRmsPx > 0.01 ? g.jitterRmsPx : 0.01);
        std::printf("%-14s rms %.3f px with smoothing, %.3f px without (%.1fx worse)\n", name.c_str(), on[name].jitterRmsPx, m.jitterRmsPx, ratio);
        if (ratio < 2.0) { std::printf("FAIL: %s: switching smoothing off is not at least 2x worse\n", name.c_str()); ++fails; }
    }
    const std::string verdict = golden::Compare(gold, off, golden::Tolerance{}, nullptr);
    if (verdict.empty()) { std::puts("FAIL: Compare() accepted the smoothing-off measurement"); ++fails; }
    else std::printf("Compare() rejects it, as it should:\n%s", verdict.c_str());
    if (!golden::Compare(gold, on, golden::Tolerance{}, nullptr).empty()) { std::puts("FAIL: the golden does not accept its own recording"); ++fails; }
    std::puts(fails ? "SENSITIVITY FAILED" : "ALL OK");
    return fails ? 1 : 0;
}
