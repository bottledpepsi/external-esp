// Recorder: what Record() queues comes back out of the file through the real esprec::Reader, bit for bit; the hot path
// allocates nothing; a full ring drops (and counts) instead of blocking.
#include "EspRec.h"
#include "Recorder.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <new>
#include <string>
#include <thread>
#include <vector>

// ---- allocation counter: counts operator new calls made while `g_count` is true on the calling thread ----------------
static thread_local bool g_count = false;
static thread_local long g_allocs = 0;
void* operator new(std::size_t n)
{
    if (g_count) ++g_allocs;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

using Clock = Recorder::Clock;
using std::chrono::microseconds;

struct Rec { uint64_t t; std::string p; };

static bool ReadAll(const char* path, esprec::Header& h, std::vector<Rec>& out, esprec::Status* end = nullptr)
{
    std::ifstream f(path, std::ios::binary);
    esprec::Reader r(f);
    if (r.ReadHeader(h) != esprec::Status::Ok) return false;
    Rec x;
    esprec::Status s;
    while ((s = r.Next(x.t, x.p)) == esprec::Status::Ok) out.push_back(x);
    if (end) *end = s;
    return true;
}

static std::string Payload(int i, size_t len)
{
    std::string s = "MCRADAR 4\n";
    s += "n=" + std::to_string(i) + "\n";
    while (s.size() < len) s.push_back(static_cast<char>('a' + (s.size() + i) % 26));
    s.resize(len);
    return s;
}

int main()
{
    const auto t0 = Clock::time_point(std::chrono::seconds(50));

    // ---- 1. round trip through the real reader ----
    {
        Recorder rec;
        std::string err;
        CHECK(rec.Start("rt.esprec", t0, &err) && rec.Active());
        std::vector<Rec> want;
        for (int i = 0; i < 500; ++i)
        {
            Rec r{static_cast<uint64_t>(i) * 8333333ULL, Payload(i, 40 + (i * 37) % 900)};
            want.push_back(r);
            CHECK(rec.Record(r.p.data(), r.p.size(), t0 + std::chrono::nanoseconds(r.t)));
        }
        rec.Stop();
        CHECK(!rec.Active() && rec.Recorded() == 500 && rec.Dropped() == 0 && !rec.Failed());
        esprec::Header h;
        std::vector<Rec> got;
        esprec::Status end{};
        CHECK(ReadAll("rt.esprec", h, got, &end) && end == esprec::Status::End);
        CHECK(h.wireVersion == 4 && h.startUnixMs > 1577836800000LL);        // after 2020: it is a real wall-clock time
        CHECK(got.size() == want.size());
        bool same = got.size() == want.size();
        for (size_t i = 0; same && i < got.size(); ++i) same = got[i].t == want[i].t && got[i].p == want[i].p;
        CHECK(same);
        rec.Stop();                                                          // twice is fine
        CHECK(!rec.Record("x", 1, t0));                                     // after Stop: refused, no crash
    }

    // ---- 2. nothing recorded -> a valid empty file ----
    {
        Recorder rec;
        CHECK(rec.Start("empty.esprec", t0));
        rec.Stop();
        esprec::Header h;
        std::vector<Rec> got;
        CHECK(ReadAll("empty.esprec", h, got) && got.empty() && h.wireVersion == 0);
    }

    // ---- 3. wrap-around: a small ring and a lot of data, producer retries when full -> every byte arrives intact ----
    {
        Recorder rec(4096);
        CHECK(rec.Start("wrap.esprec", t0));
        const int N = 3000;
        for (int i = 0; i < N; ++i)
        {
            const std::string p = Payload(i, 100 + (i * 131) % 1500);
            while (!rec.Record(p.data(), p.size(), t0 + microseconds(i * 100))) std::this_thread::yield();
        }
        rec.Stop();
        esprec::Header h;
        std::vector<Rec> got;
        CHECK(ReadAll("wrap.esprec", h, got) && static_cast<int>(got.size()) == N);
        bool same = static_cast<int>(got.size()) == N;
        for (int i = 0; same && i < N; ++i) same = got[i].p == Payload(i, 100 + (i * 131) % 1500) && got[i].t == static_cast<uint64_t>(i) * 100000ULL;
        CHECK(same);
    }

    // ---- 4. overload: nobody waits; every datagram is either recorded or counted as dropped; what was kept is intact ----
    {
        Recorder rec(4096);
        CHECK(rec.Start("drop.esprec", t0));
        const int N = 20000;
        const std::string p = Payload(7, 1400);
        int queued = 0;
        for (int i = 0; i < N; ++i) queued += rec.Record(p.data(), p.size(), t0 + microseconds(i));
        rec.Stop();
        CHECK(rec.Recorded() == static_cast<uint64_t>(queued) && rec.Recorded() + rec.Dropped() == static_cast<uint64_t>(N));
        CHECK(rec.Dropped() > 0);                                            // a 4 KiB ring cannot absorb 28 MB without the writer
        esprec::Header h;
        std::vector<Rec> got;
        CHECK(ReadAll("drop.esprec", h, got) && got.size() == rec.Recorded());
        bool ok = true;
        uint64_t prev = 0;
        for (const Rec& r : got) { ok = ok && r.p == p && r.t >= prev; prev = r.t; }
        CHECK(ok);
        std::printf("overload: %d queued, %llu dropped\n", queued, static_cast<unsigned long long>(rec.Dropped()));
    }

    // ---- 5. the hot path allocates nothing, and what it costs ----
    {
        Recorder rec;
        CHECK(rec.Start("hot.esprec", t0));
        const std::string p = Payload(1, 700);
        g_allocs = 0;
        const int N = 20000;
        double totalNs = 0, worstNs = 0;
        g_count = true;
        for (int i = 0; i < N; ++i)
        {
            const auto a = std::chrono::steady_clock::now();
            rec.Record(p.data(), p.size(), t0 + microseconds(i * 8));
            const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - a).count();
            totalNs += ns;
            worstNs = std::max(worstNs, ns);
            if ((i & 63) == 63) std::this_thread::sleep_for(microseconds(300));      // let the writer keep up, like a 120 Hz stream would
        }
        g_count = false;
        CHECK(g_allocs == 0);
        CHECK(rec.Dropped() == 0);
        std::printf("Record(): %ld allocations in %d calls; mean %.0f ns, worst %.0f ns per call (700-byte datagrams; includes ~40 ns of timer overhead)\n",
                    g_allocs, N, totalNs / N, worstNs);
        CHECK(totalNs / N < 50000.0);                                        // gross-regression guard only (50 us), not a benchmark
        rec.Stop();
    }

    // ---- 6. bad path: Start fails with a message and the recorder stays inactive ----
    {
        Recorder rec;
        std::string err;
        CHECK(!rec.Start("no/such/dir/x.esprec", t0, &err) && !err.empty() && !rec.Active());
        CHECK(!rec.Record("x", 1, t0));
    }

    // ---- 7. oversize datagram is refused, not a crash ----
    {
        Recorder rec;
        CHECK(rec.Start("big.esprec", t0));
        const std::string huge(esprec::kMaxRecordBytes + 1, 'x');
        CHECK(!rec.Record(huge.data(), huge.size(), t0) && rec.Dropped() == 1);
        rec.Stop();
    }

    std::printf(fails ? "\n%d FAILURES\n" : "\nALL OK\n", fails);
    return fails;
}
