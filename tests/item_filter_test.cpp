// item_filter_test - ItemFilter.h: wildcard matching, name normalization, the pattern list, tiers and the three modes.
#include "ConfigSchema.h"
#include "EspConfig.h"
#include "ItemFilter.h"
#include <cstdio>
using namespace itemfilter;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

static ItemState Item(const char* key, int enchants = 0)
{
    ItemState it;
    it.key = key;
    for (int i = 0; i < enchants; ++i) it.enchants.push_back({"minecraft:sharpness", 3});
    return it;
}

int main()
{
    // ---- Wild ----
    CHECK(Wild("diamond*", "diamondsword") && Wild("*sword", "diamondsword") && Wild("*", "") && Wild("**", "x") && Wild("d?amond", "diamond"));
    CHECK(!Wild("diamond", "diamondsword") && !Wild("diamond*", "golddiamond") && !Wild("a?", "a") && !Wild("", "a") && Wild("", ""));
    CHECK(Wild("a*b*c", "aXXbYYc") && !Wild("a*b*c", "aXXbYY") && Wild("*a*a*a", "bananaaa") && !Wild("*a*a*a", "bananb"));
    {
        // a pathological pattern must not blow up (the loop is iterative and bounded by n*m)
        std::string s(90, 'a'), p;
        for (int i = 0; i < 20; ++i) p += "a*";
        p += "b";
        CHECK(!Wild(p.c_str(), s.c_str()));
    }

    // ---- Normalize ----
    {
        char b[kBuf];
        CHECK(Normalize("Diamond_Sword", b, sizeof b) == 12 && std::string(b) == "diamondsword");
        CHECK(std::string(b) == "diamondsword");
        Normalize("diamondSword", b, sizeof b);
        CHECK(std::string(b) == "diamondsword");
        char tiny[5];
        CHECK(Normalize("abcdefgh", tiny, sizeof tiny) == 4 && std::string(tiny) == "abcd"); // truncates, always terminated
    }

    // ---- Rules ----
    {
        Rules r;
        CHECK(r.Set("Diamond*, *_sword,\n  ruby ore ;; ,diamond* , mymod:gem*") == 4); // empties and the duplicate dropped
        CHECK(r.Pattern(0) == "diamond*" && r.Pattern(1) == "*sword" && r.Pattern(2) == "rubyore" && r.Pattern(3) == "mymod:gem*");
        CHECK(r.Matches("minecraft:diamond_pickaxe") && r.Matches("minecraft:iron_sword") && r.Matches("mymod:ruby_ore") && r.Matches("diamondSword"));
        CHECK(!r.Matches("minecraft:stick") && !r.Matches("") );
        CHECK(r.Matches("mymod:gem_red") && !r.Matches("other:gem_red")); // a ':' pattern compares the whole id
        CHECK(Rules().Set("") == 0 && !Rules().Matches("minecraft:stick"));
        std::string many;
        for (int i = 0; i < 200; ++i) many += "item" + std::to_string(i) + ",";
        Rules m;
        CHECK(m.Set(many) == kMaxPatterns); // the cap
        Rules longp;
        CHECK(longp.Set(std::string(500, 'x')) == 1 && longp.Pattern(0).size() == kMaxPatternLen);
        // a long id never overruns the buffer
        CHECK(!r.Matches(std::string(5000, 'q')));
    }
    {
        std::string l = AddPattern("", "Diamond Sword");
        CHECK(l == "diamondsword");
        l = AddPattern(l, "gold*");
        CHECK(l == "diamondsword, gold*");
        CHECK(AddPattern(l, "GOLD*") == l && AddPattern(l, " , ") == l); // a duplicate or an empty entry changes nothing
        CHECK(RemovePattern(l, "diamondsword") == "gold*" && RemovePattern("gold*", "gold*").empty() && RemovePattern(l, "nothing") == l);
    }

    // ---- Tier ----
    CHECK(Tier(Item("minecraft:netherite_ingot")) == 3 && Tier(Item("minecraft:diamond")) == 3 && Tier(Item("minecraft:elytra")) == 3);
    CHECK(Tier(Item("minecraft:totem_of_undying")) == 3 && Tier(Item("minecraft:enchanted_golden_apple")) == 3);
    CHECK(Tier(Item("minecraft:gold_ingot")) == 2 && Tier(Item("minecraft:ender_pearl")) == 2 && Tier(Item("minecraft:white_shulker_box")) == 2);
    CHECK(Tier(Item("minecraft:iron_sword", 1)) == 2); // enchanted: at least valuable
    CHECK(Tier(Item("minecraft:rotten_flesh")) == 0 && Tier(Item("minecraft:cobblestone")) == 0);
    CHECK(Tier(Item("minecraft:rotten_flesh", 1)) == 2); // an enchanted stack is never junk
    CHECK(Tier(Item("minecraft:oak_planks")) == 1 && Tier(Item("mymod:ruby_ore")) == 1 && Tier(Item("")) == 1);
    CHECK(Tier(Item("diamondSword")) == 3); // old camelCase ids

    // ---- Show ----
    {
        Rules r;
        r.Set("diamond*, gold_ingot");
        const ItemState dia = Item("minecraft:diamond"), gold = Item("minecraft:gold_ingot"), plank = Item("minecraft:oak_planks"), rot = Item("minecraft:rotten_flesh");
        CHECK(Show(Mode::Off, r, 0, plank) && Show(Mode::Off, r, 0, rot)); // everything shows by default
        CHECK(Show(Mode::Whitelist, r, 0, dia) && Show(Mode::Whitelist, r, 0, gold) && !Show(Mode::Whitelist, r, 0, plank));
        CHECK(!Show(Mode::Blacklist, r, 0, dia) && Show(Mode::Blacklist, r, 0, plank));
        CHECK(!Show(Mode::Off, r, 1, rot) && Show(Mode::Off, r, 1, plank)); // minimum tier "No junk"
        CHECK(!Show(Mode::Off, r, 2, plank) && Show(Mode::Off, r, 2, gold) && !Show(Mode::Off, r, 3, gold) && Show(Mode::Off, r, 3, dia));
        CHECK(!Show(Mode::Whitelist, r, 3, gold)); // tier and list both have to agree
        Rules none;
        CHECK(!Show(Mode::Whitelist, none, 0, dia) && Show(Mode::Blacklist, none, 0, dia)); // an empty list: a whitelist shows nothing, a blacklist everything
    }
    {
        Mode m = Mode::Off;
        CHECK(Parse("Whitelist", m) && m == Mode::Whitelist && Parse("Blacklist", m) && m == Mode::Blacklist && !Parse("whitelist", m) && !Parse("", m));
        CHECK(std::string(TierName(0)) == "All" && std::string(TierName(3)) == "Rare" && std::string(TierName(-4)) == "All" && std::string(TierName(9)) == "Rare");
    }

    // ---- the settings file ----
    {
        Settings s;
        CHECK(s.itemFilterMode == 0 && s.itemMinTier == 0 && s.itemFilterList.empty()); // defaults change nothing
        s.itemFilterMode = 2;
        s.itemMinTier = 2;
        s.itemFilterList = "diamond*, *_sword";
        const auto back = configschema::LoadText(s.Build().ToJson());
        CHECK(back.Warnings() == 0 && back.cfg.ItemFilterMode == "Blacklist" && back.cfg.ItemMinTier == 2 && back.cfg.ItemFilterList == "diamond*, *_sword");
        Settings t;
        t.Apply(back.cfg);
        CHECK(t.itemFilterMode == 2 && t.itemMinTier == 2 && t.itemFilterList == "diamond*, *_sword");

        const auto old = configschema::LoadText(R"({"Version":1})");
        CHECK(old.Warnings() == 0 && old.cfg.ItemFilterMode == "Off" && old.cfg.ItemFilterList.empty() && old.cfg.ItemMinTier == 0);
        const auto bad = configschema::LoadText(R"({"ItemFilterMode":"Greylist","ItemMinTier":9,"ItemFilterList":42})");
        CHECK(bad.Warnings() == 3 && bad.cfg.ItemFilterMode == "Off" && bad.cfg.ItemMinTier == 3 && bad.cfg.ItemFilterList.empty());
        const std::string longList = "{\"ItemFilterList\":\"" + std::string(600, 'a') + "\"}";
        const auto cut = configschema::LoadText(longList);
        CHECK(cut.Warnings() == 1 && cut.cfg.ItemFilterList.size() == 512);
        // quotes and backslashes in a pattern survive the file
        Settings q;
        q.itemFilterList = "a\\b\"c";
        CHECK(configschema::LoadText(q.Build().ToJson()).cfg.ItemFilterList == "a\\b\"c");
    }

    if (fails) std::printf("%d failure(s)\n", fails);
    else std::printf("item_filter_test: ok\n");
    return fails ? 1 : 0;
}
