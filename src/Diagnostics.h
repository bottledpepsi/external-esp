// Diagnostics.h - the text a user pastes into a bug report ("Copy diagnostics" in the config window, or `mcesp --diagnostics [FILE]`).
//
// Everything in it passes through a Redactor first, because logs and status lines contain account names, the Windows user name and paths:
//   * account names (local player, other players, friends, names found in the agent log) -> <local>, <player1>, <player2> ... (stable within one report)
//   * the Windows user / computer name -> <user> / <host>; any C:\Users\<name> -> C:\Users\<user>; the exe and export folders -> <exe dir>
//   * UUIDs -> <uuid>; session tokens -> <token>
// Redaction is a best-effort filter, not a guarantee: read the text before posting it (docs/diagnostics.md says what it cannot know).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace diag
{
    inline constexpr size_t kMinNameLength = 3; // Minecraft names are 3..16 characters; shorter strings would also match ordinary words
    inline constexpr size_t kLogTailLines = 150;
    inline constexpr size_t kMaxReportBytes = 256 * 1024;

    class Redactor
    {
    public:
        // `label` empty -> the next free <playerN>. Names shorter than kMinNameLength are ignored. Adding a name twice is harmless.
        void AddName(const std::string& name, const std::string& label = {});
        // Every occurrence of `path` (either separator style, any case) becomes `replacement`.
        void AddPathPrefix(const std::string& path, const std::string& replacement);
        // Exact strings (the session token) that must never appear: replaced by <token>.
        void AddSecret(const std::string& secret);

        // Learns names from the lines the overlay and agent write ("Local username: X", "user=X"). Returns how many were new.
        size_t HarvestNames(const std::string& text);

        std::string Apply(const std::string& text) const;
        size_t NameCount() const { return names_.size(); }

    private:
        struct Name { std::string name, label; };
        std::vector<Name> names_;
        std::vector<std::pair<std::string, std::string>> paths_;
        std::vector<std::string> secrets_;
        size_t nextPlayer_ = 1;
    };

    // The last `maxLines` lines of `text` (whole lines only).
    std::string TailLines(const std::string& text, size_t maxLines);
    // The last `maxLines` lines of a file, reading only its last 256 KiB; "" if it cannot be read.
    std::string ReadTail(const std::string& path, size_t maxLines);

    struct Input
    {
        std::string version; // "v1.2-5-gabc123-dirty"
        std::string compiler;
        std::string os; // "Windows 11 10.0.22631"
        std::string cpu; // "16 logical processors"
        std::string renderer; // "GPU (D3D11 ...)" / "legacy GDI"
        std::string settings; // one "key = value" per line: the options that matter for rendering and the stream
        std::string hudStatus; // the status line as shown now (empty offline)
        std::string streamStats; // "published 1234, rejected 0, ..." (empty offline)
        std::string espLog, espLogBackupTail, agentLog; // raw text; the report keeps the last kLogTailLines of each
        std::string generated; // "2026-10-23 14:03:22"
    };

    // The report text (already redacted, at most kMaxReportBytes).
    std::string Build(const Input& in, const Redactor& redactor);
} // namespace diag
