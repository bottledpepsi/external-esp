// ItemFilter.h - which dropped items get a marker and a label: a value tier plus a name whitelist / blacklist. Portable, unit tested.
//
//   Tier(item)   0 junk (the HideJunk list) / 1 ordinary / 2 valuable (gold, emeralds, ender pearls, ... and anything enchanted) / 3 rare
//                (netherite, diamond, elytra, totems, nether stars, ...). "Minimum tier" hides everything below it.
//   Rules        comma- or newline-separated name patterns. `*` matches any run of characters, `?` one character. Case, spaces and underscores do
//                not matter: `Diamond Sword`, `diamond_sword` and the old `diamondSword` are the same name. A pattern with a `:` is compared with
//                the full id (`mymod:ruby*`), any other with the part after the namespace. Duplicates and empty entries are dropped.
//   Show(...)    Off: every item that passes the tier. Whitelist: only items that match a pattern. Blacklist: everything but those.
//
// Matching works on a small stack buffer: no allocation per item per frame.
#pragma once
#include "ItemText.h"
#include "Presets.h"
#include "Types.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace itemfilter
{
    enum class Mode { Off = 0, Whitelist = 1, Blacklist = 2 };
    inline const char* Name(Mode m) { return m == Mode::Whitelist ? "Whitelist" : m == Mode::Blacklist ? "Blacklist" : "Off"; }
    inline bool Parse(const std::string& s, Mode& out)
    {
        for (Mode m : {Mode::Off, Mode::Whitelist, Mode::Blacklist})
            if (s == Name(m))
            {
                out = m;
                return true;
            }
        return false;
    }
    inline const char* TierName(int t) { return t <= 0 ? "All" : t == 1 ? "No junk" : t == 2 ? "Valuable" : "Rare"; }

    inline constexpr size_t kMaxPatterns = 64, kMaxPatternLen = 48, kBuf = 96;

    // lower-case, without '_' and ' ' (that is what makes "diamondSword" == "diamond_sword"), truncated to `cap - 1`; returns the length
    inline size_t Normalize(std::string_view in, char* out, size_t cap)
    {
        size_t n = 0;
        for (char c : in)
        {
            if (c == '_' || c == ' ') continue;
            if (n + 1 >= cap) break;
            out[n++] = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        out[n] = 0;
        return n;
    }

    // `*` = any run (also empty), `?` = one character; both inputs are already normalized. Iterative, no recursion, O(n*m) worst case.
    inline bool Wild(const char* p, const char* s)
    {
        const char *star = nullptr, *mark = nullptr;
        while (*s)
        {
            if (*p == '*')
            {
                star = p++;
                mark = s;
            }
            else if (*p == '?' || *p == *s)
            {
                ++p;
                ++s;
            }
            else if (star)
            {
                p = star + 1;
                s = ++mark;
            }
            else return false;
        }
        while (*p == '*') ++p;
        return *p == 0;
    }

    class Rules
    {
    public:
        // Replaces the rules with the patterns in `text`. Returns how many were kept.
        size_t Set(const std::string& text)
        {
            pats_.clear();
            size_t i = 0;
            while (i <= text.size() && pats_.size() < kMaxPatterns)
            {
                size_t e = text.find_first_of(",\n;", i);
                if (e == std::string::npos) e = text.size();
                char buf[kMaxPatternLen + 1];
                const size_t n = Normalize(std::string_view(text).substr(i, e - i), buf, sizeof buf);
                if (n > 0 && std::find(pats_.begin(), pats_.end(), std::string(buf, n)) == pats_.end()) pats_.emplace_back(buf, n);
                i = e + 1;
            }
            return pats_.size();
        }
        size_t Size() const { return pats_.size(); }
        const std::string& Pattern(size_t i) const { return pats_[i]; }

        bool Matches(const std::string& key) const
        {
            if (pats_.empty()) return false;
            char full[kBuf], part[kBuf];
            Normalize(key, full, sizeof full);
            const size_t colon = key.find(':');
            Normalize(colon == std::string::npos ? std::string_view(key) : std::string_view(key).substr(colon + 1), part, sizeof part);
            for (const std::string& p : pats_)
                if (Wild(p.c_str(), p.find(':') != std::string::npos ? full : part)) return true;
            return false;
        }

    private:
        std::vector<std::string> pats_;
    };

    // A pattern list edited as text: add / remove one pattern, keeping the others. Used by the config window.
    inline std::string AddPattern(const std::string& list, const std::string& pattern)
    {
        Rules r;
        r.Set(list);
        Rules one;
        if (one.Set(pattern) == 0) return list;
        std::string out;
        for (size_t i = 0; i < r.Size(); ++i) out += (out.empty() ? "" : ", ") + r.Pattern(i);
        for (size_t i = 0; i < one.Size(); ++i)
        {
            bool have = false;
            for (size_t k = 0; k < r.Size(); ++k) have = have || r.Pattern(k) == one.Pattern(i);
            if (!have) out += (out.empty() ? "" : ", ") + one.Pattern(i);
        }
        return out;
    }
    inline std::string RemovePattern(const std::string& list, const std::string& pattern)
    {
        Rules r;
        r.Set(list);
        std::string out;
        for (size_t i = 0; i < r.Size(); ++i)
            if (r.Pattern(i) != pattern) out += (out.empty() ? "" : ", ") + r.Pattern(i);
        return out;
    }

    namespace detail
    {
        inline bool AnyMatch(const char* const* pats, size_t n, const char* s)
        {
            for (size_t i = 0; i < n; ++i) if (Wild(pats[i], s)) return true;
            return false;
        }
    } // namespace detail

    // 0 junk, 1 ordinary, 2 valuable, 3 rare. Names are matched on the normalized id (no namespace); the junk list is the HideJunk one.
    inline int Tier(const ItemState& it)
    {
        static const char* const kRare[] = {"netherite*", "diamond*", "elytra", "totemofundying", "enchantedgoldenapple", "netherstar", "beacon",
                                            "trident", "dragonegg", "dragonhead", "heartofthesea", "shulkershell", "conduit", "ancientdebris"};
        static const char* const kValuable[] = {"gold*", "emerald*", "enderpearl", "eyeofender", "blazerod", "ghasttear", "experiencebottle",
                                                "ironingot", "ironblock", "lapis*", "redstoneblock", "amethyst*", "obsidian", "enderchest",
                                                "*shulkerbox", "nametag", "saddle", "*horsearmor"};
        const std::string_view key = it.key;
        const size_t colon = key.find(':');
        char id[kBuf];
        Normalize(colon == std::string_view::npos ? key : key.substr(colon + 1), id, sizeof id);
        if (detail::AnyMatch(kRare, sizeof kRare / sizeof kRare[0], id)) return 3;
        if (!it.enchants.empty()) return 2; // an enchanted stack is never junk
        if (detail::AnyMatch(kValuable, sizeof kValuable / sizeof kValuable[0], id)) return 2;
        if (presets::IsJunkItem(PrettyItemName(it.key))) return 0; // the HideJunk list is in display names ("Rotten Flesh")
        return 1;
    }

    inline bool Show(Mode mode, const Rules& rules, int minTier, const ItemState& it)
    {
        if (minTier > 0 && Tier(it) < minTier) return false;
        switch (mode)
        {
        case Mode::Whitelist: return rules.Matches(it.key);
        case Mode::Blacklist: return !rules.Matches(it.key);
        default: return true;
        }
    }
} // namespace itemfilter
