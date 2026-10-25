#include "EspRec.h"
#include <algorithm>
#include <iterator>

namespace esprec
{
    namespace
    {
        void Put16(unsigned char* p, uint16_t v)
        {
            p[0] = static_cast<unsigned char>(v);
            p[1] = static_cast<unsigned char>(v >> 8);
        }

        void Put32(unsigned char* p, uint32_t v)
        {
            for (int i = 0; i < 4; ++i) p[i] = static_cast<unsigned char>(v >> (8 * i));
        }

        void Put64(unsigned char* p, uint64_t v)
        {
            for (int i = 0; i < 8; ++i) p[i] = static_cast<unsigned char>(v >> (8 * i));
        }

        uint16_t Get16(const unsigned char* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

        uint32_t Get32(const unsigned char* p)
        {
            uint32_t v = 0;
            for (int i = 3; i >= 0; --i) v = (v << 8) | p[i];
            return v;
        }

        uint64_t Get64(const unsigned char* p)
        {
            uint64_t v = 0;
            for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
            return v;
        }

        // Reads exactly n bytes; returns how many were actually available.
        size_t ReadN(std::istream& in, void* dst, size_t n)
        {
            in.read(static_cast<char*>(dst), static_cast<std::streamsize>(n));
            return static_cast<size_t>(in.gcount());
        }
    } // namespace

    void EncodeHeader(const Header& h, unsigned char out[kHeaderSize])
    {
        std::copy(std::begin(kMagic), std::end(kMagic), out);
        Put16(out + 8, h.formatVersion);
        Put16(out + 10, static_cast<uint16_t>(kHeaderSize));
        Put32(out + 12, h.flags);
        Put32(out + 16, h.wireVersion);
        Put32(out + 20, 0); // reserved
        Put64(out + 24, static_cast<uint64_t>(h.startUnixMs));
    }

    void EncodeRecordHeader(uint64_t tNs, uint32_t length, uint32_t kind, unsigned char out[kRecordHeaderSize])
    {
        Put64(out, tNs);
        Put32(out + 8, length);
        Put32(out + 12, kind);
    }

    const char* StatusText(Status s)
    {
        switch (s)
        {
        case Status::Ok: return "ok";
        case Status::End: return "end of file";
        case Status::Truncated: return "file ends inside a record (recording was cut off)";
        case Status::BadMagic: return "not an .esprec file (wrong magic)";
        case Status::BadHeader: return "corrupt or too short header";
        case Status::UnsupportedVersion: return "unsupported .esprec format version (made by a newer program?)";
        case Status::BadRecord: return "corrupt record (impossible length)";
        }
        return "?";
    }

    bool Writer::WriteHeader(const Header& h)
    {
        unsigned char b[kHeaderSize];
        EncodeHeader(h, b);
        out_.write(reinterpret_cast<const char*>(b), sizeof b);
        return static_cast<bool>(out_);
    }

    bool Writer::Append(uint64_t tNs, const void* data, size_t len)
    {
        if (len > kMaxRecordBytes) return false;
        tNs = std::max(tNs, last_);
        last_ = tNs;
        unsigned char b[kRecordHeaderSize];
        EncodeRecordHeader(tNs, static_cast<uint32_t>(len), kKindDatagram, b);
        out_.write(reinterpret_cast<const char*>(b), sizeof b);
        if (len) out_.write(static_cast<const char*>(data), static_cast<std::streamsize>(len));
        ++records_;
        return static_cast<bool>(out_);
    }

    bool Writer::Flush()
    {
        out_.flush();
        return static_cast<bool>(out_);
    }

    Status Reader::ReadHeader(Header& h)
    {
        unsigned char b[kHeaderSize];
        const size_t got = ReadN(in_, b, sizeof b);
        // Check the magic on whatever we have, so a short non-esprec file says "wrong magic", not "too short".
        const size_t cmp = std::min<size_t>(got, sizeof kMagic);
        if (!std::equal(b, b + cmp, std::begin(kMagic))) return Status::BadMagic;
        if (got < sizeof b) return Status::BadHeader;
        const uint16_t ver = Get16(b + 8), hsize = Get16(b + 10);
        if (ver == 0 || ver > kFormatVersion) return Status::UnsupportedVersion;
        if (hsize < kHeaderSize) return Status::BadHeader;
        h.formatVersion = ver;
        h.flags = Get32(b + 12);
        h.wireVersion = Get32(b + 16);
        h.startUnixMs = static_cast<int64_t>(Get64(b + 24));
        // A newer writer may have appended header fields: skip them.
        for (uint32_t left = hsize - kHeaderSize; left > 0;)
        {
            unsigned char skip[256];
            const size_t n = std::min<size_t>(left, sizeof skip);
            if (ReadN(in_, skip, n) != n) return Status::BadHeader;
            left -= static_cast<uint32_t>(n);
        }
        last_ = 0;
        return Status::Ok;
    }

    Status Reader::Next(uint64_t& tNs, std::string& payload)
    {
        for (;;)
        {
            unsigned char b[kRecordHeaderSize];
            const size_t got = ReadN(in_, b, sizeof b);
            if (got == 0) return Status::End;
            if (got < sizeof b) return Status::Truncated;
            const uint64_t t = Get64(b);
            const uint32_t len = Get32(b + 8), kind = Get32(b + 12);
            if (len > kMaxRecordBytes) return Status::BadRecord;
            payload.resize(len);
            if (len && ReadN(in_, payload.data(), len) != len) return Status::Truncated;
            if (kind != kKindDatagram)
            {
                ++skipped_;
                continue;
            }
            if (t < last_)
            {
                ++nonMonotonic_;
                tNs = last_;
            }
            else tNs = last_ = t;
            return Status::Ok;
        }
    }
} // namespace esprec
