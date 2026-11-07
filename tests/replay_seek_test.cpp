// replay_seek_test - ReplayPlayer::Seek / IndexAtNs on a fake clock, and Bookmarks (add / next / prev, the file, bad entries).
#include "Bookmarks.h"
#include "EspRec.h"
#include "Replay.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
using Clock = StreamIngest::Clock;
using std::chrono::nanoseconds;
static const Clock::time_point kBase = Clock::time_point(std::chrono::seconds(1000000));
static Clock::time_point At(double ms) { return kBase + nanoseconds(static_cast<int64_t>(ms * 1e6)); }

// 20 datagrams 100 ms apart named r0 ... r19, agent stamps 100 ms apart
static void Write(const char* path)
{
    std::ofstream f(path, std::ios::binary);
    esprec::Writer w(f);
    esprec::Header h;
    h.wireVersion = 4;
    w.WriteHeader(h);
    for (int i = 0; i < 20; ++i)
    {
        const std::string d = "MCRADAR 4\nT " + std::to_string(1000000000LL + i * 100000000LL) + "\nS ok\nL r" + std::to_string(i) + " 1 2 3 10 20 17.5\n";
        w.Append(static_cast<uint64_t>(i) * 100000000ull + 5000000ull, d.data(), d.size());
    }
    w.Flush();
}

int main()
{
    Write("seek.esprec");
    // ---- IndexAtNs / TimeNsOf ----
    {
        ReplayPlayer rp;
        CHECK(rp.Load("seek.esprec") && rp.Count() == 20);
        CHECK(rp.IndexAtNs(0) == 0 && rp.IndexAtNs(100000000) == 1 && rp.IndexAtNs(100000001) == 2 && rp.IndexAtNs(1900000000) == 19 && rp.IndexAtNs(1900000001) == 20 && rp.IndexAtNs(~0ull) == 20);
        CHECK(rp.TimeNsOf(3) == 300000000 && rp.TimeNsOf(20) == rp.DurationNs() && rp.TimeNsOf(999) == rp.DurationNs());
    }
    // ---- seek while running: forwards, backwards, to the end ----
    {
        ReplayPlayer rp;
        CHECK(rp.Load("seek.esprec"));
        StreamIngest in;
        CHECK(rp.Pump(in, At(0)) == 1 && in.Latest()->local.name == "r0");
        rp.Seek(10);
        CHECK(rp.Position() == 10 && rp.PlayheadNs() == 1000000000.0);
        CHECK(rp.Pump(in, At(1)) == 1 && in.Latest()->local.name == "r10"); // the record jumped to is delivered at once, nothing from in between
        CHECK(rp.Pump(in, At(101)) == 1 && in.Latest()->local.name == "r11"); // and it carries on from there at the normal pace
        rp.Seek(2); // backwards
        CHECK(rp.Pump(in, At(102)) == 1 && in.Latest()->local.name == "r2");
        CHECK(rp.Pump(in, At(302)) == 2 && in.Latest()->local.name == "r4");
        rp.Seek(500); // past the end: clamped, nothing more to play
        CHECK(rp.Position() == 20 && rp.Pump(in, At(400)) == 0 && rp.Finished());
        rp.Seek(19);
        CHECK(!rp.Finished() && rp.Pump(in, At(401)) == 1 && in.Latest()->local.name == "r19" && rp.Finished());
    }
    // ---- seek while paused: nothing moves until Step, which shows exactly the record jumped to ----
    {
        ReplayPlayer rp;
        CHECK(rp.Load("seek.esprec"));
        StreamIngest in;
        rp.Pump(in, At(0));
        rp.SetPaused(true);
        rp.Seek(7);
        CHECK(rp.Pump(in, At(50)) == 0 && in.PacketCount() == 1);
        CHECK(rp.Step(in, At(51)) && in.Latest()->local.name == "r7" && rp.Position() == 8 && rp.PlayheadNs() == 700000000.0);
        CHECK(rp.Step(in, At(52)) && in.Latest()->local.name == "r8");
        rp.Seek(3);
        CHECK(rp.Step(in, At(53)) && in.Latest()->local.name == "r3");
        rp.SetPaused(false);
        CHECK(rp.Pump(in, At(54)) == 0); // resuming does not burst
        CHECK(rp.Pump(in, At(154)) == 1 && in.Latest()->local.name == "r4");
    }
    // ---- a seek before the first Pump keeps its position ----
    {
        ReplayPlayer rp;
        CHECK(rp.Load("seek.esprec"));
        StreamIngest in;
        rp.Seek(5);
        CHECK(rp.Pump(in, At(0)) == 1 && in.Latest()->local.name == "r5");
        CHECK(rp.Pump(in, At(100)) == 1 && in.Latest()->local.name == "r6");
        ReplayPlayer p2;
        CHECK(p2.Load("seek.esprec"));
        StreamIngest in2;
        p2.SetPaused(true);
        p2.Seek(9);
        CHECK(p2.Step(in2, At(0)) && in2.Latest()->local.name == "r9");
    }
    // ---- speed and seeking; looping ----
    {
        ReplayPlayer rp;
        CHECK(rp.Load("seek.esprec"));
        rp.SetSpeed(2.0);
        StreamIngest in;
        rp.Pump(in, At(0));
        rp.Seek(4);
        CHECK(rp.Pump(in, At(1)) == 1 && in.Latest()->local.name == "r4");
        CHECK(rp.Pump(in, At(51)) == 1 && in.Latest()->local.name == "r5"); // 100 ms of recording at 2x = 50 ms
        ReplayPlayer lp;
        CHECK(lp.Load("seek.esprec"));
        lp.SetLoop(true);
        StreamIngest il;
        lp.Pump(il, At(0));
        lp.Seek(19);
        CHECK(lp.Pump(il, At(1)) == 1 && il.Latest()->local.name == "r19");
        lp.Pump(il, At(2000)); // runs off the end and loops
        CHECK(lp.Loops() >= 1);
        // an empty recording: Seek is harmless
        ReplayPlayer none;
        none.Seek(3);
        CHECK(none.Count() == 0 && none.Position() == 0);
    }

    // ---- the byte counter: a replay counts what it plays, in step with the datagram count ----
    {
        ReplayPlayer rp;
        CHECK(rp.Load("seek.esprec"));
        StreamIngest in;
        CHECK(in.BytesTotal() == 0);
        rp.Pump(in, At(0));
        const uint64_t one = in.BytesTotal();
        CHECK(one > 40 && one < 200 && in.PacketCount() == 1);
        rp.Pump(in, At(1000));
        CHECK(in.PacketCount() == 11 && in.BytesTotal() > one * 10 && in.BytesTotal() < one * 12);
        const char junk[] = "hello";
        in.OnDatagram(junk, 5);
        CHECK(in.BytesTotal() > one * 10 && in.RejectedCount() == 1); // a datagram that does not parse still crossed the wire
    }

    // ---- Bookmarks ----
    {
        Bookmarks b;
        CHECK(b.Size() == 0 && !b.Next(0) && !b.Prev(10) && !b.Remove(3));
        CHECK(b.Add(12, "the fight") && b.Add(3, "spawn") && b.Add(7, "  tabs\tand \x01 junk  ") && b.Add(3, "spawn point")); // same index: label replaced
        CHECK(b.Size() == 3 && b.At(0).index == 3 && b.At(0).label == "spawn point" && b.At(1).index == 7);
        CHECK(b.At(1).label == "tabsand  junk" || b.At(1).label == "tabsand junk"); // the tab and the control character are dropped, the rest stays
        CHECK(b.At(2).index == 12);
        CHECK(b.Next(0)->index == 3 && b.Next(3)->index == 7 && b.Next(7)->index == 12 && !b.Next(12) && b.Next(100) == nullptr);
        CHECK(!b.Prev(3) && b.Prev(4)->index == 3 && b.Prev(12)->index == 7 && b.Prev(13)->index == 12 && b.Prev(0) == nullptr);
        CHECK(Bookmarks::CleanLabel(std::string(100, 'x')).size() == Bookmarks::kMaxLabel && Bookmarks::CleanLabel("   ").empty() && Bookmarks::CleanLabel("é").empty());
        // the file
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / "esp_bm_test";
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        const auto file = Bookmarks::PathFor(dir / "run.esprec");
        CHECK(file.filename() == "run.esprec.bookmarks.json");
        std::vector<std::string> warn;
        Bookmarks missing;
        CHECK(missing.Load(file, 20, &warn) && missing.Size() == 0 && warn.empty()); // no file: an empty list, no complaint
        CHECK(b.Save(file));
        Bookmarks back;
        CHECK(back.Load(file, 20, &warn) && warn.empty() && back.Size() == 3 && back.At(0).index == 3 && back.At(0).label == "spawn point" && back.At(2).label == "the fight");
        Bookmarks shorter;
        CHECK(shorter.Load(file, 8, &warn) && shorter.Size() == 2 && warn.size() == 1); // 12 is past a recording of 8 records
        // bad entries are dropped one by one
        warn.clear();
        Bookmarks bad;
        CHECK(bad.FromJson(R"(["5 ok", "nope", "", "7x bad", "-3 neg", "9", "99999999999999999999 huge", "4 line\nbreak"])", 20, &warn));
        CHECK(bad.Size() == 3 && bad.At(0).index == 4 && bad.At(1).index == 5 && bad.At(2).index == 9 && bad.At(2).label.empty() && warn.size() >= 4);
        CHECK(!bad.FromJson("{\"a\":1}", 20, &warn) && bad.Size() == 0 && !bad.FromJson("[\"1 x\"", 20) && !bad.FromJson("", 20));
        CHECK(!bad.FromJson(std::string(Bookmarks::kMaxFileBytes + 1, ' '), 20));
        // the cap
        Bookmarks full;
        for (size_t i = 0; i < Bookmarks::kMax; ++i) CHECK(full.Add(i, "m"));
        CHECK(!full.Add(Bookmarks::kMax + 5, "one too many") && full.Size() == Bookmarks::kMax && full.Add(5, "relabel") && full.Remove(5) && full.Add(5000, "x"));
        { std::ofstream(dir / "x.bookmarks.json") << "garbage"; }
        CHECK(!bad.Load(dir / "x.bookmarks.json", 20));
        std::filesystem::remove_all(dir);
    }

    if (fails) std::printf("%d failure(s)\n", fails);
    else std::printf("replay_seek_test: ok\n");
    return fails ? 1 : 0;
}
