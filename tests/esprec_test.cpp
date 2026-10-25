// Tests for the .esprec recording format (src/EspRec.*): exact byte layout, round trip, truncation at every byte,
// wrong magic / version, forward-compatibility rules (bigger header, unknown record kinds, unknown flags).
#include "EspRec.h"
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

using namespace esprec;

static std::string Build(const std::vector<std::pair<uint64_t, std::string>>& recs, Header h = Header{})
{
    std::ostringstream os(std::ios::binary);
    Writer w(os);
    CHECK(w.WriteHeader(h));
    for (const auto& r : recs) CHECK(w.Append(r.first, r.second.data(), r.second.size()));
    CHECK(w.Flush());
    return os.str();
}

static std::string Bytes(const std::string& s, size_t from, size_t n) { return s.substr(from, n); }

int main()
{
    // ---- exact layout (this pins the format: changing it must bump kFormatVersion) ----
    {
        Header h;
        h.wireVersion = 4;
        h.startUnixMs = 0x0102030405060708LL;
        const std::string f = Build({{1000, "MCRADAR 4\n"}, {2500, std::string("a\0b", 3)}}, h);
        CHECK(f.size() == 32 + (16 + 10) + (16 + 3));
        CHECK(Bytes(f, 0, 8) == std::string("ESPREC\x1A\n", 8));
        CHECK(Bytes(f, 8, 2) == std::string("\x01\x00", 2));      // format_version 1
        CHECK(Bytes(f, 10, 2) == std::string("\x20\x00", 2));     // header_size 32
        CHECK(Bytes(f, 12, 4) == std::string(4, '\0'));            // flags
        CHECK(Bytes(f, 16, 4) == std::string("\x04\x00\x00\x00", 4)); // wire_version 4
        CHECK(Bytes(f, 20, 4) == std::string(4, '\0'));            // reserved
        CHECK(Bytes(f, 24, 8) == std::string("\x08\x07\x06\x05\x04\x03\x02\x01", 8)); // little-endian i64
        CHECK(Bytes(f, 32, 8) == std::string("\xE8\x03\x00\x00\x00\x00\x00\x00", 8)); // t_ns = 1000
        CHECK(Bytes(f, 40, 4) == std::string("\x0A\x00\x00\x00", 4));                 // length 10
        CHECK(Bytes(f, 44, 4) == std::string(4, '\0'));                                // kind 0
        CHECK(Bytes(f, 48, 10) == "MCRADAR 4\n");
    }

    // ---- round trip: payloads with NULs, newlines, empty, large; exact times ----
    {
        std::string big(65507, 'x');
        for (size_t i = 0; i < big.size(); i += 251) big[i] = static_cast<char>(i & 0xFF);
        const std::vector<std::pair<uint64_t, std::string>> recs = {
            {0, ""}, {1, "\n"}, {8333333, std::string("\0\xFF\r\n", 4)}, {16666667, big}, {0xFFFFFFFFFFULL, "end"}};
        Header h;
        h.wireVersion = 4;
        h.startUnixMs = 1790000000123LL;
        std::istringstream is(Build(recs, h), std::ios::binary);
        Reader r(is);
        Header got;
        CHECK(r.ReadHeader(got) == Status::Ok);
        CHECK(got.formatVersion == 1 && got.wireVersion == 4 && got.startUnixMs == 1790000000123LL && got.flags == 0);
        std::string payload;
        uint64_t t = 0;
        for (const auto& want : recs)
        {
            CHECK(r.Next(t, payload) == Status::Ok);
            CHECK(t == want.first && payload == want.second);
        }
        CHECK(r.Next(t, payload) == Status::End);
        CHECK(r.Next(t, payload) == Status::End);            // stays at the end
        CHECK(r.Skipped() == 0 && r.NonMonotonic() == 0);
    }

    // ---- an empty recording (header only) is valid ----
    {
        std::istringstream is(Build({}), std::ios::binary);
        Reader r(is);
        Header h;
        std::string p;
        uint64_t t;
        CHECK(r.ReadHeader(h) == Status::Ok && r.Next(t, p) == Status::End);
    }

    // ---- truncation at EVERY byte: complete records come back, the cut one is reported, nothing crashes ----
    {
        const std::vector<std::pair<uint64_t, std::string>> recs = {{10, "first"}, {20, "second!"}, {30, "third"}};
        const std::string full = Build(recs);
        size_t boundary[4] = {32, 32 + 16 + 5, 32 + 16 + 5 + 16 + 7, full.size()};   // end of header / rec1 / rec2 / rec3
        for (size_t cut = 0; cut <= full.size(); ++cut)
        {
            std::istringstream is(full.substr(0, cut), std::ios::binary);
            Reader r(is);
            Header h;
            const Status hs = r.ReadHeader(h);
            if (cut < 32)
            {
                // too short for a header: wrong magic if the bytes present already differ, else "bad header"
                CHECK(hs == Status::BadHeader || hs == Status::BadMagic);
                if (cut >= 1) CHECK(hs == Status::BadHeader);   // the prefix IS our magic
                continue;
            }
            CHECK(hs == Status::Ok);
            int n = 0;
            std::string p;
            uint64_t t;
            Status st;
            while ((st = r.Next(t, p)) == Status::Ok) ++n;
            int complete = 0;
            for (int i = 1; i <= 3; ++i) if (cut >= boundary[i]) complete = i;
            CHECK(n == complete);
            const bool onBoundary = cut == boundary[0] || cut == boundary[1] || cut == boundary[2] || cut == boundary[3];
            CHECK(st == (onBoundary ? Status::End : Status::Truncated));
        }
    }

    // ---- wrong magic / junk ----
    {
        const char* junk[] = {"", "MCRADAR 4\nS ok\n", "ESPREC", "ESPREX\x1A\n0123456789012345678901234567890123456789", "\x89PNG\r\n\x1A\n"};
        for (const char* j : junk)
        {
            std::istringstream is(std::string(j), std::ios::binary);
            Reader r(is);
            Header h;
            const Status s = r.ReadHeader(h);
            CHECK(s == Status::BadMagic || s == Status::BadHeader);
            if (std::strncmp(j, "MCRADAR", 7) == 0 || std::strncmp(j, "ESPREX", 6) == 0 || std::strncmp(j, "\x89PNG", 4) == 0) CHECK(s == Status::BadMagic);
        }
        // a raw datagram dump is not accepted as a recording
        std::istringstream is(std::string("MCRADAR 4\nS ok\nL A 1 2 3\n"), std::ios::binary);
        Reader r(is);
        Header h;
        CHECK(r.ReadHeader(h) == Status::BadMagic);
    }

    // ---- format version: 0 and a future major are refused; the current one accepted ----
    {
        std::string f = Build({{5, "x"}});
        std::string v2 = f;
        v2[8] = 2;
        std::string v0 = f;
        v0[8] = 0;
        for (const std::string* s : {&v2, &v0})
        {
            std::istringstream is(*s, std::ios::binary);
            Reader r(is);
            Header h;
            CHECK(r.ReadHeader(h) == Status::UnsupportedVersion);
        }
    }

    // ---- forward compatibility: bigger header, unknown flags, unknown record kinds ----
    {
        std::string f = Build({{5, "keep"}});
        // header_size 32 -> 48 with 16 extra bytes that an old reader must skip; unknown flag bits set
        f[10] = 48;
        f[12] = static_cast<char>(0xF0);
        f.insert(32, std::string(16, '\x7F'));
        // append a record of an unknown kind (7), then another datagram
        unsigned char rh[kRecordHeaderSize];
        EncodeRecordHeader(6, 4, 7, rh);
        f.append(reinterpret_cast<char*>(rh), sizeof rh);
        f.append("zzzz");
        EncodeRecordHeader(9, 4, kKindDatagram, rh);
        f.append(reinterpret_cast<char*>(rh), sizeof rh);
        f.append("tail");
        std::istringstream is(f, std::ios::binary);
        Reader r(is);
        Header h;
        std::string p;
        uint64_t t;
        CHECK(r.ReadHeader(h) == Status::Ok && h.flags == 0xF0);
        CHECK(r.Next(t, p) == Status::Ok && t == 5 && p == "keep");
        CHECK(r.Next(t, p) == Status::Ok && t == 9 && p == "tail");     // the kind-7 record was skipped
        CHECK(r.Skipped() == 1 && r.Next(t, p) == Status::End);
        // header_size smaller than the v1 header is corrupt
        std::string bad = Build({});
        bad[10] = 16;
        std::istringstream is2(bad, std::ios::binary);
        Reader r2(is2);
        CHECK(r2.ReadHeader(h) == Status::BadHeader);
        // header_size promises bytes that are not there
        std::string cut = Build({});
        cut[10] = 100;
        std::istringstream is3(cut, std::ios::binary);
        Reader r3(is3);
        CHECK(r3.ReadHeader(h) == Status::BadHeader);
    }

    // ---- corrupt records: impossible length stops the reader; time going backwards is clamped and counted ----
    {
        std::string f = Build({{100, "a"}});
        unsigned char rh[kRecordHeaderSize];
        EncodeRecordHeader(200, kMaxRecordBytes + 1, kKindDatagram, rh);
        f.append(reinterpret_cast<char*>(rh), sizeof rh);
        std::istringstream is(f, std::ios::binary);
        Reader r(is);
        Header h;
        std::string p;
        uint64_t t;
        CHECK(r.ReadHeader(h) == Status::Ok && r.Next(t, p) == Status::Ok && r.Next(t, p) == Status::BadRecord);

        std::string g = Build({{100, "a"}});
        EncodeRecordHeader(50, 1, kKindDatagram, rh);       // goes back in time
        g.append(reinterpret_cast<char*>(rh), sizeof rh);
        g.append("b");
        std::istringstream is2(g, std::ios::binary);
        Reader r2(is2);
        CHECK(r2.ReadHeader(h) == Status::Ok && r2.Next(t, p) == Status::Ok && t == 100);
        CHECK(r2.Next(t, p) == Status::Ok && t == 100 && p == "b" && r2.NonMonotonic() == 1);
    }

    // ---- writer: clamps backwards times, refuses oversize payloads ----
    {
        std::ostringstream os(std::ios::binary);
        Writer w(os);
        CHECK(w.WriteHeader(Header{}));
        CHECK(w.Append(100, "a", 1) && w.Append(50, "b", 1));
        const std::string big(kMaxRecordBytes + 1, 'x');
        CHECK(!w.Append(200, big.data(), big.size()));
        CHECK(w.Records() == 2);
        std::istringstream is(os.str(), std::ios::binary);
        Reader r(is);
        Header h;
        std::string p;
        uint64_t t;
        CHECK(r.ReadHeader(h) == Status::Ok && r.Next(t, p) == Status::Ok && r.Next(t, p) == Status::Ok && t == 100);
        CHECK(r.NonMonotonic() == 0);
    }

    std::printf(fails ? "\n%d FAILURES\n" : "\nALL OK\n", fails);
    return fails;
}
