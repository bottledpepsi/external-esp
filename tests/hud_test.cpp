// hud_test - the status text: what each mode shows, when the second line appears, bounds, and that Build() allocates nothing after the first call.
#include "Hud.h"
#include <cstdio>
#include <cstdlib>
#include <new>
using namespace hud;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

// count heap allocations while armed
static bool g_count = false;
static long g_allocs = 0;
void* operator new(size_t n) { if (g_count) ++g_allocs; if (void* p = std::malloc(n)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

static Facts Healthy()
{
    Facts f;
    f.agentLive = true; f.haveCam = true; f.fps = 262.4f; f.cpuMs = 0.8f; f.sampleHz = 118.2f; f.ageMs = 6.4f; f.jitterMs = 0.8f; f.lossPct = 0.f;
    f.cam = Cam::Exact; f.status = "agent connected"; f.predictionErrPx = 1.5f;
    return f;
}
static bool Has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

int main()
{
    Text t;

    // ---- Compact, healthy: one short line ----
    t.Build(Mode::Compact, Healthy());
    CHECK(t.Lines() == 1);
    CHECK(t.Line(0) == "external-esp  |  active  |  262 fps  |  118 Hz  |  age 6 ms  |  loss 0.0%");
    CHECK(!t.Alert(0));

    // ---- Compact: a second line only when something needs attention ----
    {
        Facts f = Healthy();
        f.agentLive = false; f.haveCam = false; f.status = "waiting for minecraft";
        t.Build(Mode::Compact, f);
        CHECK(t.Lines() == 2 && Has(t.Line(0), "waiting") && !Has(t.Line(0), "Hz") && t.Line(1) == "waiting for minecraft" && t.Alert(1));
        f = Healthy();
        f.link = "UDP: 3 lost, 1 reordered of 12000"; f.lossPct = 0.2f;
        t.Build(Mode::Compact, f);
        CHECK(t.Lines() == 2 && Has(t.Line(0), "loss 0.2%") && Has(t.Line(1), "3 lost") && t.Alert(1));
        f = Healthy(); f.recording = true; f.recorded = 1234;
        t.Build(Mode::Compact, f);
        CHECK(t.Lines() == 2 && t.Line(1) == "REC 1234" && !t.Alert(1));
        f.dropped = 5;
        t.Build(Mode::Compact, f);
        CHECK(Has(t.Line(1), "dropped") && t.Alert(1));
        f = Healthy(); f.demo = true; f.agentLive = false; f.status = "";
        t.Build(Mode::Compact, f);
        CHECK(t.Lines() == 1 && Has(t.Line(0), "demo") && !Has(t.Line(0), "Hz"));
        f = Healthy(); f.visible = false;
        t.Build(Mode::Compact, f);
        CHECK(Has(t.Line(0), "hidden"));
    }
    // ---- Off: nothing, except the exit countdown ----
    {
        t.Build(Mode::Off, Healthy());
        CHECK(t.Lines() == 0);
        Facts f = Healthy(); f.exiting = true; f.exitLeftS = 1.25f;
        for (Mode m : {Mode::Off, Mode::Compact, Mode::Detailed})
        {
            t.Build(m, f);
            CHECK(t.Lines() == 1 && t.Line(0) == "external-esp  |  exiting in 1.2s (release to cancel)" && t.Alert(0));
        }
        f.exitLeftS = -3;
        t.Build(Mode::Compact, f);
        CHECK(Has(t.Line(0), "exiting in 0.0s"));
    }
    // ---- Detailed: everything ----
    {
        Facts f = Healthy();
        f.lostTotal = 7; f.reorderedTotal = 2; f.lossPct = 0.1f; f.snapStatus = "world: 12 players"; f.recording = true; f.recorded = 99; f.dropped = 1;
        t.Build(Mode::Detailed, f);
        CHECK(t.Lines() == 3);
        CHECK(t.Line(0) == "external-esp  |  active  |  262 fps  |  0.80 ms cpu  |  gpu");
        CHECK(Has(t.Line(1), "agent connected") && Has(t.Line(1), "cam: exact") && Has(t.Line(1), "world: 12 players") && Has(t.Line(1), "REC 99 (1 dropped)"));
        CHECK(Has(t.Line(2), "agent  118 Hz") && Has(t.Line(2), "data age 6.4 ms") && Has(t.Line(2), "jitter 0.8 ms") && Has(t.Line(2), "lost 7 (0.1%)") &&
              Has(t.Line(2), "reordered 2") && Has(t.Line(2), "guess off 1.5 px"));
        f.cam = Cam::ExactThirdPerson; f.gpu = false; f.ageMs = -1; f.jitterMs = -1; f.predictionErrPx = -1;
        t.Build(Mode::Detailed, f);
        CHECK(Has(t.Line(0), "gdi") && Has(t.Line(1), "third person") && !Has(t.Line(2), "data age") && !Has(t.Line(2), "jitter") && !Has(t.Line(2), "guess"));
        f.agentLive = false;
        t.Build(Mode::Detailed, f);
        CHECK(t.Lines() == 2 && t.Alert(1));
    }
    // ---- Detailed: the overlay's own cost line (only when the overlay supplies it) ----
    {
        Facts f = Healthy();
        t.Build(Mode::Detailed, f);
        CHECK(t.Lines() == 3); // perfValid = false: nothing added, the older layout is unchanged
        f.perfValid = true; f.cacheHitPct = 99.25f; f.cacheClears = 2; f.labelsDropped = 3;
        t.Build(Mode::Detailed, f);
        CHECK(t.Lines() == 4 && Has(t.Line(3), "text cache  hit 99.2%") && Has(t.Line(3), "emptied 2 x") && Has(t.Line(3), "labels dropped 3") && !t.Alert(3));
        f.cacheHitPct = 41.f;
        t.Build(Mode::Detailed, f);
        CHECK(t.Alert(3)); // a low rate is flagged
        f.cacheHitPct = -1.f;
        t.Build(Mode::Detailed, f);
        CHECK(Has(t.Line(3), "text cache  idle") && !t.Alert(3));
        t.Build(Mode::Compact, f);
        CHECK(t.Lines() == 1); // Compact stays one line
    }
    // ---- bounded: absurd input cannot overrun a line ----
    {
        std::string long1(2000, 'x');
        Facts f = Healthy();
        f.status = long1; f.link = long1; f.snapStatus = long1;
        for (Mode m : {Mode::Compact, Mode::Detailed})
        {
            t.Build(m, f);
            for (int i = 0; i < t.Lines(); ++i) CHECK(t.Line(i).size() < 384);
        }
        f = Healthy(); f.fps = 1e30f; f.sampleHz = -1e30f; f.lostTotal = ~0ull; f.ageMs = 1e30f;
        t.Build(Mode::Detailed, f);
        for (int i = 0; i < t.Lines(); ++i) CHECK(t.Line(i).size() < 384);
    }
    // ---- no allocation after the first call, whatever the numbers do ----
    {
        Text a;
        Facts f = Healthy();
        f.link = "UDP: 3 lost, 1 reordered of 12000"; f.snapStatus = "world: 12 players"; f.recording = true;
        f.perfValid = true; f.cacheHitPct = 98.f;
        a.Build(Mode::Detailed, f);                          // warm-up
        a.Build(Mode::Compact, f);
        g_allocs = 0;
        g_count = true;
        for (int i = 0; i < 2000; ++i)
        {
            f.fps = 100.f + static_cast<float>(i % 300);
            f.sampleHz = static_cast<float>(i % 240);
            f.ageMs = static_cast<float>(i % 17) * 0.37f;
            f.recorded = static_cast<uint64_t>(i) * 977;
            f.cacheHitPct = (i % 7) == 0 ? -1.f : static_cast<float>(i % 101);
            f.labelsDropped = i % 9;
            f.cacheClears = static_cast<uint64_t>(i);
            f.agentLive = (i % 5) != 0;
            a.Build(i % 3 == 0 ? Mode::Detailed : i % 3 == 1 ? Mode::Compact : Mode::Off, f);
        }
        g_count = false;
        std::printf("allocations in 2000 builds: %ld\n", g_allocs);
        CHECK(g_allocs == 0);
    }
    // ---- mode names ----
    {
        Mode m = Mode::Off;
        CHECK(Parse("Detailed", m) && m == Mode::Detailed && Parse("Off", m) && m == Mode::Off && !Parse("detailed", m) && !Parse("", m));
        CHECK(Next(Mode::Off) == Mode::Compact && Next(Mode::Compact) == Mode::Detailed && Next(Mode::Detailed) == Mode::Off);
        CHECK(std::string(Name(Mode::Compact)) == "Compact");
    }

    if (fails) std::printf("%d failure(s)\n", fails); else std::puts("hud_test: all passed");
    return fails ? 1 : 0;
}
