// AgentJar.h - the agent jar carried inside the exe. Portable core: hashing, extraction into a cache folder, verification, clean-up.
// (Reading the bytes out of the exe's resource section is Windows-only and lives in AgentJarWin.cpp.)
//
// The jar is written once to  %LOCALAPPDATA%\external-esp\agent\esp-agent-<first 12 hex of its SHA-256>.jar  and handed to the attach helper from
// there. The name carries the hash, so a new exe with a new jar never overwrites the file an old, still running game has open; a file whose
// content no longer matches its name (a half-written copy, a virus scanner's change) is rewritten before use.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace agentjar
{
    // ---- SHA-256 (FIPS 180-4), no dependencies ----
    class Sha256
    {
    public:
        Sha256() { Reset(); }
        void Reset();
        void Update(const void* data, size_t n);
        std::array<uint8_t, 32> Final();

    private:
        void Block(const uint8_t* p);
        uint32_t h_[8];
        uint8_t buf_[64];
        size_t fill_ = 0;
        uint64_t total_ = 0;
    };
    std::string Sha256Hex(const void* data, size_t n);
    std::string HashFileHex(const std::filesystem::path& p); // "" when the file cannot be read

    struct Blob
    {
        const unsigned char* data = nullptr;
        size_t size = 0;
        bool Valid() const { return data && size > 0; }
    };

    inline constexpr size_t kHashChars = 12; // in the file name
    inline constexpr const char* kPrefix = "esp-agent-";

    std::string FileNameFor(const std::string& hashHex); // esp-agent-<12 hex>.jar

    struct Extracted
    {
        enum class How { Created, Reused, Repaired, Failed };
        How how = How::Failed;
        std::filesystem::path path;
        std::string hash; // full SHA-256 of the blob
        std::string error; // when Failed
    };
    // Makes sure the folder holds an intact copy of `blob` and returns its path. Intact = the file's hash equals the blob's. Never leaves a partial
    // jar under the final name (written to a temporary file, flushed, then renamed).
    Extracted EnsureExtracted(const Blob& blob, const std::filesystem::path& cacheDir);

    struct Cleaned
    {
        int removed = 0;
        int kept = 0; // files that could not be removed (a running game holds them open) - fine, they are tried again next time
    };
    // Removes esp-agent-*.jar (and stray *.tmp) except `keepName`. Failures are counted, never thrown.
    Cleaned CleanOld(const std::filesystem::path& cacheDir, const std::string& keepName);
} // namespace agentjar
