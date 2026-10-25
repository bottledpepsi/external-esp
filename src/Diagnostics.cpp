#include "Diagnostics.h"
#include <algorithm>
#include <cctype>
#include <fstream>

namespace diag
{
    namespace
    {
        bool IsWord(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || (static_cast<unsigned char>(c) >= 0x80); }
        char Lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

        // find `needle` in `hay` from `from`, ASCII-case-insensitive; npos if none
        size_t FindNoCase(const std::string& hay, const std::string& needle, size_t from)
        {
            if (needle.empty() || hay.size() < needle.size()) return std::string::npos;
            for (size_t i = from; i + needle.size() <= hay.size(); ++i)
            {
                size_t k = 0;
                while (k < needle.size() && Lower(hay[i + k]) == Lower(needle[k])) ++k;
                if (k == needle.size()) return i;
            }
            return std::string::npos;
        }

        std::string ReplaceAllNoCase(const std::string& s, const std::string& from, const std::string& to, bool wholeWord)
        {
            std::string out;
            size_t pos = 0;
            for (;;)
            {
                const size_t at = FindNoCase(s, from, pos);
                if (at == std::string::npos) break;
                const bool before = at > 0 && IsWord(s[at - 1]), after = at + from.size() < s.size() && IsWord(s[at + from.size()]);
                if (wholeWord && (before || after))
                {
                    out.append(s, pos, at + 1 - pos); // not a whole word: keep one char and keep looking
                    pos = at + 1;
                    continue;
                }
                out.append(s, pos, at - pos);
                out += to;
                pos = at + from.size();
            }
            out.append(s, pos, std::string::npos);
            return out;
        }

        bool IsHex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

        // 8-4-4-4-12 hex groups -> <uuid>
        std::string RedactUuids(const std::string& s)
        {
            static const int groups[5] = {8, 4, 4, 4, 12};
            std::string out;
            for (size_t i = 0; i < s.size();)
            {
                size_t p = i;
                bool ok = !(i > 0 && IsWord(s[i - 1]));
                for (int g = 0; ok && g < 5; ++g)
                {
                    for (int k = 0; ok && k < groups[g]; ++k, ++p) ok = p < s.size() && IsHex(s[p]);
                    if (ok && g < 4) ok = p < s.size() && s[p] == '-', ++p;
                }
                if (ok && !(p < s.size() && IsWord(s[p]))) { out += "<uuid>"; i = p; }
                else out += s[i++];
            }
            return out;
        }

        // C:\Users\<anything>  or  C:/Users/<anything>  -> C:\Users\<user>   (also /home/<name> for the Linux test hosts)
        std::string RedactUserDirs(const std::string& s)
        {
            std::string out;
            for (size_t i = 0; i < s.size();)
            {
                size_t p = i;
                std::string prefix;
                if (i + 9 <= s.size() && std::isalpha(static_cast<unsigned char>(s[i])) && s[i + 1] == ':' && (s[i + 2] == '\\' || s[i + 2] == '/') &&
                    FindNoCase(s.substr(i + 3, 5), "users", 0) == 0 && (s[i + 8] == '\\' || s[i + 8] == '/'))
                {
                    prefix = s.substr(i, 9);
                    p = i + 9;
                }
                else if (i + 6 <= s.size() && s.compare(i, 6, "/home/") == 0 && !(i > 0 && IsWord(s[i - 1])))
                {
                    prefix = "/home/";
                    p = i + 6;
                }
                if (prefix.empty()) { out += s[i++]; continue; }
                size_t q = p;
                while (q < s.size() && s[q] != '\\' && s[q] != '/' && s[q] != '"' && s[q] != '\'' && s[q] != ' ' && s[q] != '\t' && s[q] != ';' && s[q] != '\r' && s[q] != '\n') ++q;
                if (q == p || s.compare(p, q - p, "<user>") == 0 || s.compare(p, q - p, "%USERPROFILE%") == 0) { out += prefix; i = p; continue; }
                out += prefix + "<user>";
                i = q;
            }
            return out;
        }

        // token=XXXX / "K XXXX" lines (any value of 16+ letters/digits after token=) -> token=<token>
        std::string RedactTokenFields(const std::string& s)
        {
            std::string out;
            size_t pos = 0;
            for (;;)
            {
                const size_t at = FindNoCase(s, "token=", pos);
                if (at == std::string::npos) break;
                out.append(s, pos, at + 6 - pos);
                size_t q = at + 6;
                while (q < s.size() && std::isalnum(static_cast<unsigned char>(s[q]))) ++q;
                out += q - (at + 6) >= 8 ? "<token>" : s.substr(at + 6, q - (at + 6));
                pos = q;
            }
            out.append(s, pos, std::string::npos);
            return out;
        }

        // value after `key` up to a delimiter, e.g. "Local username: " -> "Alice"
        void HarvestAfter(const std::string& text, const std::string& key, const std::string& stops, std::vector<std::string>& out)
        {
            size_t pos = 0;
            for (;;)
            {
                const size_t at = text.find(key, pos);
                if (at == std::string::npos) break;
                size_t p = at + key.size(), q = p;
                while (q < text.size() && stops.find(text[q]) == std::string::npos) ++q;
                if (q > p && q - p <= 32) out.push_back(text.substr(p, q - p));
                pos = q > pos ? q : pos + 1;
            }
        }
    } // namespace

    void Redactor::AddName(const std::string& name, const std::string& label)
    {
        if (name.size() < kMinNameLength || name.size() > 64) return;
        for (const Name& n : names_)
            if (n.name.size() == name.size() && FindNoCase(n.name, name, 0) == 0) return;
        names_.push_back({name, label.empty() ? "<player" + std::to_string(nextPlayer_++) + ">" : label});
    }

    void Redactor::AddPathPrefix(const std::string& path, const std::string& replacement)
    {
        if (path.size() < 4) return;
        std::string back = path, fwd = path;
        std::replace(back.begin(), back.end(), '/', '\\');
        std::replace(fwd.begin(), fwd.end(), '\\', '/');
        while (back.size() > 3 && (back.back() == '\\')) back.pop_back();
        while (fwd.size() > 3 && (fwd.back() == '/')) fwd.pop_back();
        paths_.push_back({back, replacement});
        if (fwd != back) paths_.push_back({fwd, replacement});
    }

    void Redactor::AddSecret(const std::string& secret)
    {
        if (secret.size() >= 8) secrets_.push_back(secret);
    }

    size_t Redactor::HarvestNames(const std::string& text)
    {
        std::vector<std::string> found;
        HarvestAfter(text, "Local username: ", " \t\r\n(", found);
        HarvestAfter(text, "user=", " \t\r\n;\"'", found);
        const size_t before = names_.size();
        for (const std::string& n : found)
        {
            if (n == "(unknown" || n.find('.') != std::string::npos || n.find(':') != std::string::npos || n.find('/') != std::string::npos || n.find('\\') != std::string::npos) continue; // class names, paths
            AddName(n);
        }
        return names_.size() - before;
    }

    std::string Redactor::Apply(const std::string& text) const
    {
        std::string s = text;
        for (const std::string& sec : secrets_) s = ReplaceAllNoCase(s, sec, "<token>", false);
        // longest path first, so "C:\Users\bob\app" wins over "C:\Users\bob"
        auto paths = paths_;
        std::sort(paths.begin(), paths.end(), [](const auto& a, const auto& b) { return a.first.size() > b.first.size(); });
        for (const auto& p : paths) s = ReplaceAllNoCase(s, p.first, p.second, false);
        s = RedactUserDirs(s);
        // longest name first: "BobTheBuilder" before "Bob"
        auto names = names_;
        std::sort(names.begin(), names.end(), [](const Name& a, const Name& b) { return a.name.size() > b.name.size(); });
        for (const Name& n : names) s = ReplaceAllNoCase(s, n.name, n.label, true);
        s = RedactUuids(s);
        s = RedactTokenFields(s);
        return s;
    }

    std::string TailLines(const std::string& text, size_t maxLines)
    {
        if (maxLines == 0 || text.empty()) return {};
        size_t pos = text.size();
        if (text.back() == '\n') --pos; // do not count the final newline as an empty line
        size_t lines = 0;
        while (pos > 0)
        {
            const size_t nl = text.rfind('\n', pos - 1);
            if (nl == std::string::npos) return text;
            if (++lines == maxLines) return text.substr(nl + 1);
            pos = nl;
        }
        return text;
    }

    std::string ReadTail(const std::string& path, size_t maxLines)
    {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) return {};
        constexpr std::streamoff kWindow = 256 * 1024;
        const std::streamoff size = f.tellg(), start = size > kWindow ? size - kWindow : 0;
        f.seekg(start);
        std::string s(static_cast<size_t>(size - start), '\0');
        f.read(s.data(), static_cast<std::streamsize>(s.size()));
        s.resize(static_cast<size_t>(f.gcount()));
        if (start > 0) // we may have started in the middle of a line: drop it
        {
            const size_t nl = s.find('\n');
            s = nl == std::string::npos ? std::string() : s.substr(nl + 1);
        }
        return TailLines(s, maxLines);
    }

    std::string Build(const Input& in, const Redactor& redactor)
    {
        std::string r;
        auto section = [&](const char* title, const std::string& body)
        {
            r += "\n--- " + std::string(title) + " ---\n";
            r += body.empty() ? "(none)\n" : (body.back() == '\n' ? body : body + "\n");
        };
        r += "=== external-esp diagnostics (names, paths, ids and tokens are redacted - read it before posting) ===\n";
        r += "generated: " + in.generated + "\n";
        r += "version:   " + in.version + "\n";
        r += "compiler:  " + in.compiler + "\n";
        r += "os:        " + in.os + "\n";
        r += "cpu:       " + in.cpu + "\n";
        r += "renderer:  " + in.renderer + "\n";
        section("settings", in.settings);
        section("HUD status", in.hudStatus);
        section("stream", in.streamStats);
        section("esp_log.txt (last lines)", TailLines(in.espLog, kLogTailLines));
        if (TailLines(in.espLog, kLogTailLines).size() == in.espLog.size() && !in.espLogBackupTail.empty())
            section("esp_log.1.txt (end of the previous log)", TailLines(in.espLogBackupTail, kLogTailLines));
        section("esp_agent.log (last lines)", TailLines(in.agentLog, kLogTailLines));
        std::string out = redactor.Apply(r);
        if (out.size() > kMaxReportBytes) out = out.substr(0, kMaxReportBytes - 40) + "\n... (cut at 256 KiB)\n";
        return out;
    }
} // namespace diag
