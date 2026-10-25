// Datagram numbering (wire v5 Q line): parsing, loss / reorder / duplicate accounting, agent restarts, wrap-around, replay bypass.
#include "EspRec.h"
#include "Replay.h"
#include "StreamIngest.h"
#include "Wire.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#ifndef ESP_TEST_DATA_DIR
#error "ESP_TEST_DATA_DIR must point at tests/data"
#endif

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

using Clock = StreamIngest::Clock;
using std::chrono::seconds;

static std::string Dg(const std::string& session, const std::string& seq, const std::string& extra = "L A 1 2 3\n")
{
    return "MCRADAR 5\nQ " + session + " " + seq + "\nS ok\n" + extra;
}
static std::string Dg(uint32_t session, uint64_t seq)
{
    char s[16];
    std::snprintf(s, sizeof s, "%x", static_cast<unsigned>(session));
    return Dg(s, std::to_string(seq));
}
static bool Feed(StreamIngest& in, const std::string& d) { return in.OnDatagram(d.data(), d.size()); }

int main()
{
    // ---- the Q line ----
    {
        WorldSnapshot s;
        const std::string a = Dg("abcdef01", "7");
        CHECK(ParseDatagram(a.data(), a.size(), s) && s.hasWireSeq && s.wireSession == 0xABCDEF01u && s.wireSeq == 7);
        const std::string b = Dg("0", "0"), c = Dg("FFFFFFFF", "4294967295");
        WorldSnapshot s2, s3;
        CHECK(ParseDatagram(b.data(), b.size(), s2) && s2.hasWireSeq && s2.wireSession == 0 && s2.wireSeq == 0);
        CHECK(ParseDatagram(c.data(), c.size(), s3) && s3.wireSession == 0xFFFFFFFFu && s3.wireSeq == 0xFFFFFFFFu);
        for (const char* bad : {"Q abcdef01\n", "Q zz 5\n", "Q abcdef012 5\n", "Q abcdef01 -1\n", "Q abcdef01 4294967296\n", "Q abcdef01 1.5\n", "Q  \n", "Q 0x10 5\n"})
        {
            const std::string d = std::string("MCRADAR 5\n") + bad + "S ok\n";
            WorldSnapshot w;
            CHECK(ParseDatagram(d.data(), d.size(), w) && !w.hasWireSeq && w.ignoredLines == 1);
        }
        const std::string bare = "MCRADAR 5\nQ\nS ok\n";                                  // a bare "Q" is too short to be a line at all: skipped
        WorldSnapshot wb;
        CHECK(ParseDatagram(bare.data(), bare.size(), wb) && !wb.hasWireSeq);
        const std::string v4 = "MCRADAR 4\nQ abcdef01 7\n";
        WorldSnapshot w4;
        CHECK(ParseDatagram(v4.data(), v4.size(), w4) && !w4.hasWireSeq);              // Q means nothing before v5
        std::ifstream f(std::string(ESP_TEST_DATA_DIR) + "/wire/v5_full.dgram", std::ios::binary);
        const std::string j((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        WorldSnapshot js;
        if (j.empty()) std::printf("tests/data/wire/v5_full.dgram is missing or empty: the corpus is made by `sh agent/test/run-java-tests.sh --update-corpus`; check the file arrived intact (git am, not patch(1))\n");
        CHECK(!j.empty() && ParseDatagram(j.data(), j.size(), js) && js.hasWireSeq && js.wireSession == 0xABCDEF01u && js.wireSeq == 7);   // from the real Java encoder
    }

    // ---- in order: nothing to report ----
    {
        StreamIngest in;
        for (uint64_t i = 1; i <= 1000; ++i) CHECK(Feed(in, Dg(1, i)));
        const auto l = in.Link();
        CHECK(l.numbered == 1000 && l.lost == 0 && l.reordered == 0 && l.duplicates == 0 && l.sessions == 1 && in.LinkSummary().empty() && in.PacketCount() == 1000);
    }

    // ---- gaps count the missing numbers ----
    {
        StreamIngest in;
        Feed(in, Dg(1, 1));
        CHECK(Feed(in, Dg(1, 2)) && Feed(in, Dg(1, 5)));                              // 3 and 4 never came
        CHECK(in.Link().lost == 2 && in.PacketCount() == 3);
        CHECK(Feed(in, Dg(1, 1000)) && in.Link().lost == 2 + 994);
        CHECK(in.LinkSummary().find("996 lost") != std::string::npos && in.LinkSummary().find("of 4") != std::string::npos);
    }

    // ---- a late datagram: dropped, and the loss it caused is taken back ----
    {
        StreamIngest in;
        Feed(in, Dg(1, 1));
        Feed(in, Dg(1, 2));
        CHECK(Feed(in, Dg(1, 4)) && in.Link().lost == 1);                             // 3 looks lost...
        CHECK(!Feed(in, Dg(1, 3)));                                                    // ...then it shows up behind 4: dropped
        CHECK(in.Link().lost == 0 && in.Link().reordered == 1 && in.PacketCount() == 3);
        CHECK(Feed(in, Dg(1, 5)) && in.Link().lost == 0);                              // numbering continues from 4
        // very late ones never push `lost` below zero
        CHECK(!Feed(in, Dg(1, 2)) && !Feed(in, Dg(1, 1)) && in.Link().lost == 0 && in.Link().reordered == 3);
    }

    // ---- duplicates ----
    {
        StreamIngest in;
        Feed(in, Dg(1, 10));
        CHECK(!Feed(in, Dg(1, 10)) && in.Link().duplicates == 1 && in.Link().lost == 0 && in.PacketCount() == 1);
        CHECK(Feed(in, Dg(1, 11)) && !Feed(in, Dg(1, 11)) && in.Link().duplicates == 2);
    }

    // ---- an agent restart (new session id) starts over; it is NOT mistaken for reordering ----
    {
        StreamIngest in;
        for (uint64_t i = 1; i <= 500; ++i) Feed(in, Dg(0xAAAA, i));
        CHECK(Feed(in, Dg(0xBBBB, 1)) && Feed(in, Dg(0xBBBB, 2)));                      // numbers went back from 500 to 1: fine
        CHECK(in.Link().sessions == 2 && in.Link().reordered == 0 && in.Link().lost == 0 && in.PacketCount() == 502);
        CHECK(Feed(in, Dg(0xBBBB, 4)) && in.Link().lost == 1);
        // a straggler of the OLD session after the restart is treated as a new session too (we cannot know better); the next one of the new session restarts again
        CHECK(Feed(in, Dg(0xAAAA, 501)) && in.Link().sessions == 3);
    }

    // ---- wrap-around at 2^32 ----
    {
        StreamIngest in;
        Feed(in, Dg(1, 0xFFFFFFFEull));
        CHECK(Feed(in, Dg(1, 0xFFFFFFFFull)) && Feed(in, Dg(1, 0)) && Feed(in, Dg(1, 1)));      // ...FFFE, FFFF, 0, 1: in order across the wrap
        CHECK(in.Link().lost == 0 && in.Link().reordered == 0);
        CHECK(Feed(in, Dg(1, 3)) && in.Link().lost == 1);                                    // 2 missing after the wrap
        CHECK(!Feed(in, Dg(1, 0xFFFFFFFFull)) && in.Link().reordered == 1);                  // a very old one from before the wrap is behind us
    }

    // ---- datagrams without numbers (v1-v4, or v5 without Q) are untouched by all of this ----
    {
        StreamIngest in;
        const std::string v4 = "MCRADAR 4\nS ok\nL A 1 2 3\n", noQ = "MCRADAR 5\nS ok\nL A 1 2 3\n";
        for (int i = 0; i < 10; ++i) CHECK(Feed(in, v4) && Feed(in, noQ));
        CHECK(in.Link().numbered == 0 && in.PacketCount() == 20);
        // mixed: numbered ones are tracked while unnumbered ones pass through
        CHECK(Feed(in, Dg(1, 1)) && Feed(in, v4) && Feed(in, Dg(1, 2)) && in.Link().numbered == 2 && in.Link().lost == 0);
    }

    // ---- the live socket path: a datagram that is not PUBLISHED (a newer one was waiting) still counts for loss accounting ----
    {
        // UdpListener drains the socket, calls NoteSequence for each datagram and publishes only the newest accepted one. Same here, without a socket:
        StreamIngest in;
        std::shared_ptr<WorldSnapshot> back;
        for (uint64_t seq : {1, 2, 3, 5, 6})                                              // 4 was lost on the way
        {
            auto snap = std::make_shared<WorldSnapshot>();
            const std::string d = Dg(7, seq);
            CHECK(ParseDatagram(d.data(), d.size(), *snap));
            if (in.NoteSequence(*snap)) back = snap;
        }
        in.Publish(back, Clock::now());                                                      // only the newest of the five is published
        CHECK(in.PacketCount() == 1 && in.Link().lost == 1 && in.Link().numbered == 5);
    }

    // ---- replay does not use the check: a looping recording restarts its numbers ----
    {
        std::ofstream f("seq.esprec", std::ios::binary);
        esprec::Writer w(f);
        w.WriteHeader(esprec::Header{});
        for (uint64_t i = 1; i <= 3; ++i)
        {
            const std::string d = Dg(9, i);
            w.Append((i - 1) * 8000000, d.data(), d.size());
        }
        w.Flush();
        f.close();
        ReplayPlayer rp;
        CHECK(rp.Load("seq.esprec"));
        rp.SetLoop(true);
        StreamIngest in;
        in.SetSeqCheck(false);
        const Clock::time_point t0 = Clock::time_point(seconds(5));
        for (int ms = 0; ms <= 100; ++ms) rp.Pump(in, t0 + std::chrono::milliseconds(ms));
        CHECK(rp.Loops() >= 2 && in.PacketCount() >= 9 && in.Link().numbered == 0);
    }

    std::printf(fails ? "\n%d FAILURES\n" : "\nALL OK\n", fails);
    return fails;
}
