// ItemText.h - turning the agent's item strings into display text (platform independent, unit tested).
#pragma once
#include "Types.h"
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

// "%7BName%22" -> "{Name\"";  "-" (the wire's "empty") -> "".
inline std::string PercentDecode(const std::string& s)
{
    if (s == "-") return {};
    std::string o;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '%' && i + 2 < s.size() + 0 && std::isxdigit(static_cast<unsigned char>(s[i + 1])) && std::isxdigit(
            static_cast<unsigned char>(s[i + 2])))
        {
            o += static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        }
        else o += s[i];
    }
    return o;
}

// "minecraft:diamond_sword" -> "Diamond Sword";  "diamondSword" -> "Diamond Sword";
// "minecraft:potion.effect.healing" -> "Potion (Healing)";  "mymod:ruby_ore" -> "Ruby Ore".
inline std::string PrettyItemName(const std::string& key)
{
    std::string s = key;
    size_t colon = s.find(':');
    if (colon != std::string::npos) s = s.substr(colon + 1);
    std::string suffix;
    size_t eff = s.find(".effect.");
    if (eff != std::string::npos)
    {
        suffix = s.substr(eff + 8);
        s = s.substr(0, eff);
    }
    auto words = [](const std::string& in)
    {
        std::string o;
        for (size_t i = 0; i < in.size(); ++i)
        {
            char c = in[i];
            if (c == '_' || c == '/' || c == '.')
            {
                if (!o.empty() && o.back() != ' ') o += ' ';
                continue;
            }
            if (i > 0 && std::isupper(static_cast<unsigned char>(c)) &&
                std::islower(static_cast<unsigned char>(in[i - 1])) && !o.empty() && o.back() != ' ') o += ' ';
            // camelCase
            if (o.empty() || o.back() == ' ') c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            o += c;
        }
        return o;
    };
    std::string name = words(s);
    if (!suffix.empty()) name += " (" + words(suffix) + ")";
    return name.empty() ? key : name;
}

// "minecraft:sharpness@5,minecraft:unbreaking@3" -> [{sharpness,5},{unbreaking,3}]
inline std::vector<Enchant> ParseEnchants(const std::string& s)
{
    std::vector<Enchant> out;
    size_t i = 0;
    while (i < s.size())
    {
        size_t comma = s.find(',', i);
        std::string item = s.substr(i, comma == std::string::npos ? std::string::npos : comma - i);
        size_t at = item.rfind('@');
        if (at != std::string::npos && at > 0)
        {
            Enchant e;
            e.id = item.substr(0, at);
            const long lv = std::strtol(item.c_str() + at + 1, nullptr, 10); // not atoi: it is undefined when the number overflows
            e.level = lv > 1000000 ? 1000000 : (lv < -1000000 ? -1000000 : static_cast<int>(lv));
            if (!e.id.empty()) out.push_back(e);
        }
        if (comma == std::string::npos) break;
        i = comma + 1;
    }
    return out;
}

inline bool IsCurse(const Enchant& e) { return e.id.find("curse") != std::string::npos; }

// {sharpness,5} -> "Sharpness V";  levels > 10 print as numbers;  legacy numeric ids -> "Enchant #16 V".
inline std::string PrettyEnchant(const Enchant& e)
{
    static const char* const roman[] = {"", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X"};
    std::string name = PrettyItemName(e.id);
    if (!e.id.empty() && std::isdigit(static_cast<unsigned char>(e.id[0]))) name = "Enchant #" + e.id;
    const bool single = e.level == 1 && (e.id.find("mending") != std::string::npos || IsCurse(e) || e.id.
        find("silk_touch") != std::string::npos ||
        e.id.find("infinity") != std::string::npos || e.id.find("flame") != std::string::npos ||
        e.id.find("channeling") != std::string::npos || e.id.find("multishot") != std::string::npos);
    if (single) return name; // vanilla hides the "I" on one-level enchantments
    if (e.level >= 1 && e.level <= 10) return name + " " + roman[e.level];
    return name + " " + std::to_string(e.level);
}
