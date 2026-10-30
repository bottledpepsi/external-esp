// adapter_choice_test.cpp - GPU adapter selection on hand-made machines (src/AdapterChoice.h).
#include "../src/AdapterChoice.h"
#include <cstdio>

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

using namespace gpuselect;

static Adapter A(int i, const char* name, bool software, bool owns, unsigned vendor = 0x10DE)
{
    Adapter a;
    a.index = i; a.name = name; a.software = software; a.ownsOutput = owns; a.vendor = vendor;
    return a;
}

int main()
{
    const Preference none;
    // a desktop with one GPU
    { auto d = Choose({A(0, "NVIDIA GeForce RTX 4070", false, true)}, none); CHECK(d.index == -1 && d.reason.find("only hardware adapter") != std::string::npos); }
    // a hybrid laptop: the iGPU drives the panel, the dGPU renders games -> the overlay goes to the iGPU
    std::vector<Adapter> laptop{A(0, "NVIDIA GeForce RTX 4060 Laptop GPU", false, false), A(1, "Intel(R) UHD Graphics", false, true, 0x8086), A(2, "Microsoft Basic Render Driver", true, false, 0x1414)};
    { auto d = Choose(laptop, none); CHECK(d.index == 1 && d.reason.find("drives the primary monitor") != std::string::npos); }
    // the same laptop with the dGPU wired to the display (MUX switch)
    { std::vector<Adapter> l{A(0, "NVIDIA GeForce RTX 4060 Laptop GPU", false, true), A(1, "Intel(R) UHD Graphics", false, false, 0x8086)}; auto d = Choose(l, none); CHECK(d.index == 0); }
    // two GPUs both with outputs / none known: D3D's default, with the reason
    { std::vector<Adapter> l{A(0, "A", false, true), A(1, "B", false, true)}; auto d = Choose(l, none); CHECK(d.index == -1 && d.reason.find("adapter 0") != std::string::npos); }
    { std::vector<Adapter> l{A(0, "A", false, false), A(1, "B", false, false)}; auto d = Choose(l, none); CHECK(d.index == -1); }
    // a VM / RDP session: only the software adapter
    { auto d = Choose({A(0, "Microsoft Basic Render Driver", true, true, 0x1414)}, none); CHECK(d.index == -1 && d.reason.find("software") != std::string::npos); }
    // an owner that is software is never chosen over hardware
    { std::vector<Adapter> l{A(0, "NVIDIA", false, false), A(1, "Microsoft Basic Render Driver", true, true, 0x1414)}; auto d = Choose(l, none); CHECK(d.index == -1); }
    // explicit choices
    { Preference p; p.index = 0; auto d = Choose(laptop, p); CHECK(d.index == 0 && d.reason == "chosen with --adapter 0"); }
    { Preference p; p.name = "nvidia"; auto d = Choose(laptop, p); CHECK(d.index == 0 && d.reason.find("\"nvidia\"") != std::string::npos); }
    { Preference p; p.name = "INTEL"; auto d = Choose(laptop, p); CHECK(d.index == 1); }
    // a bad preference is ignored, said so, and the normal choice follows
    { Preference p; p.index = 9; auto d = Choose(laptop, p); CHECK(d.index == 1 && d.reason.find("does not exist") != std::string::npos && d.reason.find("drives the primary monitor") != std::string::npos); }
    { Preference p; p.name = "radeon"; auto d = Choose(laptop, p); CHECK(d.index == 1 && d.reason.find("no adapter matches") != std::string::npos); }
    // nothing could be listed
    { auto d = Choose({}, none); CHECK(d.index == -1 && d.reason.find("no adapters") != std::string::npos); }
    // the flag text
    CHECK(ParsePreference("1").index == 1 && ParsePreference("1").name.empty());
    CHECK(ParsePreference("nvidia").index == -1 && ParsePreference("nvidia").name == "nvidia");
    CHECK(ParsePreference("").index == -1 && ParsePreference("").name.empty());
    CHECK(ParsePreference("12a").name == "12a");
    std::printf(g_fail == 0 ? "ALL OK (%d checks)\n" : "%d FAILURES\n", g_fail == 0 ? g_checks : g_fail);
    return g_fail == 0 ? 0 : 1;
}
