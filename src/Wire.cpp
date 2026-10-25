#include "Wire.h"
#include "ItemText.h"
#include <charconv>
#include <cmath>
#include <string_view>

namespace
{
    using std::string_view;

    // Splits a line into space/tab separated tokens, left to right.
    struct Tokens
    {
        string_view rest;

        bool Next(string_view& tok)
        {
            size_t i = 0;
            while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) ++i;
            if (i == rest.size())
            {
                rest = {};
                return false;
            }
            size_t j = i;
            while (j < rest.size() && rest[j] != ' ' && rest[j] != '\t') ++j;
            tok = rest.substr(i, j - i);
            rest = rest.substr(j);
            return true;
        }
    };

    // A plain decimal number: digits, one optional '.', optional leading '-', optional exponent. Rejects nan, inf, hex floats, '+', ' ' and
    // everything std::strtod would also swallow; the result must be finite.
    bool ParseNumber(string_view s, double& out)
    {
        if (s.empty() || s.size() > 40) return false;
        for (char c : s)
            if (!((c >= '0' && c <= '9') || c == '.' || c == '-' || c == 'e' || c == 'E')) return false;
        double v = 0;
        const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
        if (r.ec != std::errc() || r.ptr != s.data() + s.size() || !std::isfinite(v)) return false;
        out = v;
        return true;
    }

    // "-" (or anything invalid / out of [lo, hi]) -> false, leaving `out` alone.
    bool Num(string_view s, float& out, double lo, double hi)
    {
        double v;
        if (!ParseNumber(s, v) || v < lo || v > hi) return false;
        out = static_cast<float>(v);
        return true;
    }

    bool Coord(string_view s, float& out) { return Num(s, out, -wire::kMaxCoord, wire::kMaxCoord); }
    bool Angle(string_view s, float& out) { return Num(s, out, -wire::kMaxAngle, wire::kMaxAngle); }

    bool Integer(string_view s, long long& out)
    {
        if (s.empty() || s.size() > 20) return false;
        const auto r = std::from_chars(s.data(), s.data() + s.size(), out);
        return r.ec == std::errc() && r.ptr == s.data() + s.size();
    }

    // The header line: "MCRADAR <1-3 digits>", optionally followed by a space and more text (ignored). "MCRADAR 4junk" is not ours.
    int HeaderVersion(string_view line)
    {
        constexpr string_view magic = "MCRADAR ";
        if (line.substr(0, magic.size()) != magic) return 0;
        size_t i = magic.size(), v = 0, digits = 0;
        while (i < line.size() && line[i] >= '0' && line[i] <= '9' && digits < 4)
        {
            v = v * 10 + static_cast<size_t>(line[i] - '0');
            ++i;
            ++digits;
        }
        if (digits == 0 || digits > 3) return 0;
        if (i < line.size() && line[i] != ' ' && line[i] != '\t') return 0;
        return static_cast<int>(v);
    }

    void CapEnchants(std::vector<Enchant>& e)
    {
        if (e.size() > wire::kMaxEnchants) e.resize(wire::kMaxEnchants);
    }
} // namespace

namespace wire
{
    std::string CleanText(const std::string& s, size_t maxBytes)
    {
        std::string o;
        o.reserve(std::min(s.size(), maxBytes));
        size_t i = 0;
        while (i < s.size())
        {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            size_t len = 1; // bytes of the sequence at i, and what to emit for it
            std::string emit;
            if (c < 0x20 || c == 0x7F) emit = "?";
            else if (c < 0x80) emit = std::string(1, static_cast<char>(c));
            else
            {
                // decode one UTF-8 sequence, rejecting overlong forms, surrogates and > U+10FFFF
                size_t need = 0;
                uint32_t cp = 0, minCp = 0;
                if (c >= 0xC2 && c <= 0xDF) { need = 1; cp = c & 0x1F; minCp = 0x80; }
                else if (c >= 0xE0 && c <= 0xEF) { need = 2; cp = c & 0x0F; minCp = 0x800; }
                else if (c >= 0xF0 && c <= 0xF4) { need = 3; cp = c & 0x07; minCp = 0x10000; }
                bool ok = need > 0 && i + need < s.size(); // all continuation bytes present
                if (ok)
                    for (size_t k = 1; k <= need; ++k)
                    {
                        const unsigned char cc = static_cast<unsigned char>(s[i + k]);
                        if ((cc & 0xC0) != 0x80) { ok = false; break; }
                        cp = (cp << 6) | (cc & 0x3F);
                    }
                if (ok && (cp < minCp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))) ok = false;
                if (ok)
                {
                    len = need + 1;
                    emit = s.substr(i, len);
                }
                else emit = "\xEF\xBF\xBD"; // U+FFFD; consumes just the one bad byte, so the next byte gets a fresh chance
            }
            if (o.size() + emit.size() > maxBytes) break;
            o += emit;
            i += len;
        }
        return o;
    }
} // namespace wire

ParseResult ParseDatagramEx(const char* data, size_t len, WorldSnapshot& out)
{
    const string_view all(data ? data : "", data ? len : 0);
    size_t pos = all.find('\n');
    string_view first = all.substr(0, pos);
    if (!first.empty() && first.back() == '\r') first.remove_suffix(1);
    const int version = HeaderVersion(first);
    if (version == 0) return {ParseStatus::NotOurs, 0};
    if (version < wire::kMinVersion || version > wire::kMaxVersion) return {ParseStatus::UnsupportedVersion, version};

    WorldSnapshot snap;
    snap.protocolVersion = version;
    snap.status = "agent connected";
    if (version >= 4) snap.hasItems = false; // until an F line says the item list follows
    auto ignore = [&] { if (snap.ignoredLines < 0xFFFFFFFFu) ++snap.ignoredLines; };

    while (pos != string_view::npos && pos + 1 <= all.size())
    {
        const size_t start = pos + 1;
        const size_t nl = all.find('\n', start);
        string_view line = all.substr(start, nl == string_view::npos ? string_view::npos : nl - start);
        pos = nl;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.size() < 2 || line[1] != ' ') continue;
        const char tag = line[0];
        const string_view body = line.substr(2);

        if (tag == 'S')
        {
            snap.status = wire::CleanText(std::string(body), wire::kMaxStatusBytes);
            continue;
        }
        if (tag == 'T' && version >= 4)
        {
            Tokens t{body};
            string_view tok;
            long long v;
            if (t.Next(tok) && Integer(tok, v))
            {
                snap.sampleNs = static_cast<int64_t>(v);
                snap.hasSampleTime = true;
            }
            else ignore();
            continue;
        }
        if (tag == 'F' && version >= 4)
        {
            snap.hasItems = true;
            continue;
        }

        Tokens ls{body};
        string_view name, sx, sy, sz, tok;
        if (tag == 'L' || tag == 'P')
        {
            if (!(ls.Next(name) && ls.Next(sx) && ls.Next(sy) && ls.Next(sz))) { ignore(); continue; }
            string_view syaw = "-", spitch = "-", shealth = "-", sw = "-", sh = "-", uuid = "-";
            if (ls.Next(tok)) syaw = tok; // v1: yaw only, v2: + pitch/health
            if (ls.Next(tok)) spitch = tok;
            if (ls.Next(tok)) shealth = tok;
            if (version >= 3) // v3: + hitbox size, uuid
            {
                if (ls.Next(tok)) sw = tok;
                if (ls.Next(tok)) sh = tok;
                if (ls.Next(tok)) uuid = tok;
            }
            PlayerState p;
            float x, y, z;
            if (!Coord(sx, x) || !Coord(sy, y) || !Coord(sz, z)) { ignore(); continue; }
            if (tag == 'P' && snap.players.size() >= wire::kMaxPlayers) { ignore(); continue; }
            p.name = wire::CleanText(std::string(name), wire::kMaxNameBytes);
            p.pos = {x, y, z};
            p.yawValid = Angle(syaw, p.yawDeg);
            p.pitchValid = Angle(spitch, p.pitchDeg);
            p.healthValid = Num(shealth, p.health, 0, wire::kMaxHealth);
            float w = 0, h = 0;
            p.sizeValid = Num(sw, w, 1e-6, wire::kMaxBox) && Num(sh, h, 1e-6, wire::kMaxBox);
            if (p.sizeValid)
            {
                p.width = w;
                p.height = h;
            }
            p.uuid = uuid == "-" ? std::string() : wire::CleanText(std::string(uuid), wire::kMaxUuidBytes);
            if (tag == 'L')
            {
                snap.local = std::move(p);
                snap.localValid = true;
            }
            else snap.players.push_back(std::move(p));
        }
        else if (tag == 'C' && version >= 3)
        {
            string_view syaw, spitch, sfov, smode;
            if (!(ls.Next(sx) && ls.Next(sy) && ls.Next(sz) && ls.Next(syaw) && ls.Next(spitch) && ls.Next(sfov) && ls.Next(smode))) { ignore(); continue; }
            float x, y, z, yaw, pitch;
            if (!Coord(sx, x) || !Coord(sy, y) || !Coord(sz, z) || !Angle(syaw, yaw) || !Angle(spitch, pitch)) { ignore(); continue; }
            CameraState& c = snap.camera;
            c.valid = true;
            c.pos = {x, y, z};
            c.yawDeg = yaw;
            c.pitchDeg = pitch;
            c.fovValid = Num(sfov, c.fovDeg, wire::kMinFov, wire::kMaxFov); // 0, 180, negative or absurd: unknown (the configured FOV is used)
            long long mode = 1;
            c.mode = (Integer(smode, mode) && mode >= 1 && mode <= 3) ? static_cast<int>(mode) : 1;
        }
        else if (tag == 'I' && version >= 3)
        {
            string_view sw, sh, scount, key, ench, tags;
            if (!(ls.Next(sx) && ls.Next(sy) && ls.Next(sz) && ls.Next(sw) && ls.Next(sh) && ls.Next(scount) && ls.Next(key) && ls.Next(ench) && ls.Next(tags)))
            {
                ignore();
                continue;
            }
            float x, y, z, w = 0, h = 0;
            if (!Coord(sx, x) || !Coord(sy, y) || !Coord(sz, z)) { ignore(); continue; }
            if (snap.items.size() >= wire::kMaxItems) { ignore(); continue; }
            ItemState it;
            it.pos = {x, y, z};
            it.sizeValid = Num(sw, w, 1e-6, wire::kMaxBox) && Num(sh, h, 1e-6, wire::kMaxBox);
            if (it.sizeValid)
            {
                it.width = w;
                it.height = h;
            }
            double cnt = 1;
            if (!ParseNumber(scount, cnt) || cnt < 1) cnt = 1; // clamp BEFORE converting: casting a huge or NaN double to int is undefined
            it.count = cnt > wire::kMaxCount ? wire::kMaxCount : static_cast<int>(cnt);
            it.key = wire::CleanText(PercentDecode(std::string(key)), wire::kMaxKeyBytes);
            it.enchants = ParseEnchants(wire::CleanText(PercentDecode(std::string(ench)), wire::kMaxEnchantTextBytes));
            CapEnchants(it.enchants);
            it.tags = wire::CleanText(PercentDecode(std::string(tags)), wire::kMaxTagsBytes);
            snap.items.push_back(std::move(it));
        }
    }
    out = std::move(snap);
    return {ParseStatus::Ok, version};
}
