// golden_runner [--update] [--rel R] [--abs PX] [--csv FILE] <recording.esprec> <golden.json>
//   default : measure the recording and compare with the golden numbers; exit 0 = within tolerance, 1 = regression, 2 = usage/IO error
//   --csv F : also write every probe's per-frame screen position and jitter deviation to F (frame,time_s,probe,x,y,dev) for plotting
//   --update: write the golden file from the current measurement (do this on purpose, via tests/regen-goldens.sh, and review the diff)
#include "golden.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

int main(int argc, char** argv)
{
    bool update = false;
    golden::Tolerance tol;
    const char* csv = nullptr;
    const char* pos[2] = {nullptr, nullptr};
    int np = 0;
    for (int i = 1; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--update")) update = true;
        else if (!std::strcmp(argv[i], "--rel") && i + 1 < argc) tol.relative = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--abs") && i + 1 < argc) tol.absolutePx = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--csv") && i + 1 < argc) csv = argv[++i];
        else if (np < 2) pos[np++] = argv[i];
        else np = 3;
    }
    if (np != 2)
    {
        std::fputs("usage: golden_runner [--update] [--rel R] [--abs PX] <recording.esprec> <golden.json>\n", stderr);
        return 2;
    }
    golden::Options opt;
    golden::Result golden, measured;
    std::string err;
    std::ifstream gf(pos[1]);
    const bool haveGolden = static_cast<bool>(gf);
    if (haveGolden)
    {
        const std::string text((std::istreambuf_iterator<char>(gf)), std::istreambuf_iterator<char>());
        if (!golden::FromJson(text, opt, golden, &err))
        {
            std::fprintf(stderr, "%s: %s\n", pos[1], err.c_str());
            if (!update) return 2;
        }
    }
    if (update) opt = golden::Options{};   // an updated golden file always records the default measurement settings
    golden::Tracks tracks;
    if (!golden::Measure(pos[0], opt, measured, &err, csv ? &tracks : nullptr))
    {
        std::fprintf(stderr, "%s: %s\n", pos[0], err.c_str());
        return 2;
    }
    if (csv)
    {
        std::ofstream c(csv, std::ios::trunc);
        c << "frame,time_s,probe,x,y,dev\n";
        for (const auto& [name, tr] : tracks)
            for (size_t i = 0; i < tr.size(); ++i)
            {
                if (std::isnan(tr[i].first)) continue;
                double dev = std::nan("");
                if (i > 0 && i + 1 < tr.size() && !std::isnan(tr[i - 1].first) && !std::isnan(tr[i + 1].first))
                    dev = std::hypot(tr[i].first - (tr[i - 1].first + tr[i + 1].first) / 2, tr[i].second - (tr[i - 1].second + tr[i + 1].second) / 2);
                c << i << ',' << static_cast<double>(i) / opt.frameHz << ',' << name << ',' << tr[i].first << ',' << tr[i].second << ',' << dev << '\n';
            }
        std::printf("wrote %s\n", csv);
    }
    std::printf("%-22s %8s %10s %10s %10s %10s\n", "probe", "frames", "rms px", "p95 px", "max px", "step px");
    for (const auto& [name, m] : measured)
        std::printf("%-22s %8.0f %10.4f %10.4f %10.4f %10.4f\n", name.c_str(), m.frames, m.jitterRmsPx, m.jitterP95Px, m.jitterMaxPx, m.maxStepPx);
    if (update)
    {
        std::ofstream o(pos[1], std::ios::trunc);
        o << golden::ToJson(std::filesystem::path(pos[0]).filename().string(), opt, measured);
        std::printf("wrote %s\n", pos[1]);
        return o ? 0 : 2;
    }
    if (!haveGolden)
    {
        std::fprintf(stderr, "%s does not exist; create it on purpose with --update\n", pos[1]);
        return 2;
    }
    std::string notes;
    const std::string fail = golden::Compare(golden, measured, tol, &notes);
    if (!notes.empty()) std::printf("note: %s", notes.c_str());
    if (!fail.empty())
    {
        std::printf("GOLDEN FAILURES:\n%s", fail.c_str());
        return 1;
    }
    std::puts("golden: within tolerance");
    return 0;
}
