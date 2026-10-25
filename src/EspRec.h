// EspRec.h - the .esprec recording format: the agent's raw UDP datagrams plus their arrival times, so a session can be
// replayed (--replay) or used as a test input without the game. Platform independent; documented in docs/esprec.md.
//
//   file   = header (32 bytes at format version 1, size given by the header itself) , record*
//   header = magic[8] "ESPREC\x1A\n" | u16 format_version | u16 header_size | u32 flags | u32 wire_version | u32 reserved
//            | i64 start_unix_ms                                                       (all little-endian)
//   record = u64 t_ns | u32 length | u32 kind | payload[length]                         (kind 0 = one raw datagram)
//
// Forward compatibility: a reader accepts every file whose format_version it knows, skips header bytes it does not know
// (header_size), ignores unknown flag bits and skips records of an unknown kind. An incompatible change bumps format_version.
// There is no footer, so a recording that was cut off (crash, kill) is still readable up to its last complete record.
#pragma once
#include <cstddef>
#include <cstdint>
#include <istream>
#include <ostream>
#include <string>

namespace esprec
{
    inline constexpr unsigned char kMagic[8] = {'E', 'S', 'P', 'R', 'E', 'C', 0x1A, '\n'};
    inline constexpr uint16_t kFormatVersion = 1;
    inline constexpr uint32_t kHeaderSize = 32; // size of the header written at format version 1
    inline constexpr uint32_t kRecordHeaderSize = 16;
    inline constexpr uint32_t kMaxRecordBytes = 1u << 20; // a UDP datagram is < 64 KB; anything bigger is a corrupt length
    inline constexpr uint32_t kKindDatagram = 0;

    struct Header
    {
        uint16_t formatVersion = kFormatVersion;
        uint32_t flags = 0; // none defined yet; readers ignore unknown bits
        uint32_t wireVersion = 0; // MCRADAR version of the first datagram, 0 = unknown (informational)
        int64_t startUnixMs = 0; // wall clock at the start of the recording (informational)
    };

    // ---- byte-level helpers (no I/O) ----
    void EncodeHeader(const Header& h, unsigned char out[kHeaderSize]);
    void EncodeRecordHeader(uint64_t tNs, uint32_t length, uint32_t kind, unsigned char out[kRecordHeaderSize]);

    class Writer
    {
    public:
        explicit Writer(std::ostream& out) : out_(out)
        {
        }

        bool WriteHeader(const Header& h);
        // One datagram that arrived tNs nanoseconds after the start of the recording. Times that go backwards are clamped.
        bool Append(uint64_t tNs, const void* data, size_t len);
        bool Flush();
        uint64_t Records() const { return records_; }

    private:
        std::ostream& out_;
        uint64_t last_ = 0, records_ = 0;
    };

    enum class Status
    {
        Ok, // header read / a record was returned
        End, // clean end of file (no partial record)
        Truncated, // the file ends inside a record; everything before it was returned
        BadMagic, // not an .esprec file
        BadHeader, // header shorter than it claims, or header_size < 32
        UnsupportedVersion, // format_version this reader does not know
        BadRecord, // a record length > kMaxRecordBytes: corrupt, stop
    };

    const char* StatusText(Status s);

    class Reader
    {
    public:
        explicit Reader(std::istream& in) : in_(in)
        {
        }

        Status ReadHeader(Header& h); // must be called once, first
        // Next datagram. `payload` is overwritten (its capacity is reused). Records of an unknown kind are skipped.
        Status Next(uint64_t& tNs, std::string& payload);

        uint64_t Skipped() const { return skipped_; } // records of an unknown kind
        uint64_t NonMonotonic() const { return nonMonotonic_; } // times that went backwards (clamped)

    private:
        std::istream& in_;
        uint64_t last_ = 0, skipped_ = 0, nonMonotonic_ = 0;
    };
} // namespace esprec
