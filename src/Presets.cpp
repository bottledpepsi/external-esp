#include "Presets.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace presets
{
    const std::vector<float> Opacities{0.f, 0.10f, 0.20f, 0.30f};
    const std::vector<std::string> FillOpacityLabels{"Off", "10%", "20%", "30%"};
    const std::vector<float> TraceOpacities{0.25f, 0.50f, 0.75f, 1.00f};
    const std::vector<std::string> TraceOpacityLabels{"25%", "50%", "75%", "100%"};
    const std::vector<float> CornerRadius{2.f, 4.f, 6.f};
    const std::vector<std::string> CornerRadiusLabels{"Small", "Normal", "Large"};
    const std::vector<float> Thickness{1.f, 2.f, 3.f};
    const std::vector<std::string> ThicknessLabels{"1px", "2px", "3px"};
    const std::vector<float> NameSizes{10.f, 13.f, 17.f};
    const std::vector<std::string> NameSizeLabels{"Small", "Normal", "Large"};
    const std::vector<float> LabelDist{25.f, 50.f, 100.f, kUnlimited};
    const std::vector<std::string> LabelDistLabels{"25m", "50m", "100m", "Unlimited"};
    const std::vector<float> PlayerDist{50.f, 100.f, 200.f, kUnlimited};
    const std::vector<float> EntityDist{32.f, 64.f, 128.f, kUnlimited};
    const std::vector<std::string> EntityDistLabels{"32m", "64m", "128m", "Unlimited"};
    const std::vector<std::string> EntityPaletteNames{"Red", "Orange", "Yellow", "Green", "Cyan", "Blue", "Magenta", "White", "Gray"};
    const std::vector<Rgb> EntityPaletteRgb{{1.f, 0.25f, 0.25f}, {1.f, 0.60f, 0.15f}, {1.f, 0.92f, 0.25f}, {0.30f, 0.90f, 0.35f}, {0.30f, 0.90f, 0.95f},
                                            {0.35f, 0.55f, 1.f}, {1.f, 0.35f, 0.90f}, {1.f, 1.f, 1.f}, {0.60f, 0.62f, 0.68f}};
    int EntityPaletteIndex(const std::string& name, int fallback)
    {
        for (size_t i = 0; i < EntityPaletteNames.size(); ++i)
            if (EntityPaletteNames[i] == name) return static_cast<int>(i);
        return fallback;
    }
    const std::vector<std::string> PlayerDistLabels{"50m", "100m", "200m", "Unlimited"};
    const std::vector<float> OverlayOpacity{1.00f, 0.85f, 0.70f};
    const std::vector<std::string> OverlayOpacityLabels{"100%", "85%", "70%"};
    const std::vector<int> FpsCaps{60, 120, 0};
    const std::vector<std::string> FpsCapLabels{"60", "120", "Unlimited"};
    const std::vector<int> MinimapSizes{120, 160, 220};
    const std::vector<std::string> MinimapSizeLabels{"Small", "Normal", "Large"};
    const std::vector<int> MinimapRanges{50, 100, 200};
    const std::vector<std::string> MinimapRangeLabels{"50m", "100m", "200m"};
    const std::vector<int> Fovs{60, 70, 80, 90, 100, 110};
    const std::vector<std::string> FovLabels{"60", "70 (default)", "80", "90", "100", "110 (Quake Pro)"};

    const std::vector<std::string> DistFormatLabels{"Meters", "Kilometers", "Off"};
    const std::vector<std::string> BoxStyleLabels{"Rounded", "Sharp", "Corner-only", "Skeleton"};
    const std::vector<std::string> BoxColorLabels{"Static", "Gradient"};
    const std::vector<std::string> TracerOriginLabels{"Bottom Center", "Screen Center"};
    const std::vector<std::string> TracerColorLabels{"Dynamic", "White"};
    const std::vector<std::string> TracerTargetLabels{"Feet", "Center", "Head"};

    namespace
    {
        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
            return s;
        }

        const std::unordered_set<std::string>& Junk()
        {
            static const std::unordered_set<std::string> j = []
            {
                const char* items[] = {
                    "Arrow", "Bone", "Rotten Flesh", "Spider Eye", "String", "Gunpowder", "Gravel", "Dirt", "Sand",
                    "Cobblestone", "Stick",
                    "Wooden Sword", "Wooden Pickaxe", "Wooden Axe", "Wooden Shovel", "Wooden Hoe",
                    "Leather Helmet", "Leather Chestplate", "Leather Leggings", "Leather Boots",
                    "Feather", "Zombie Flesh", "Poisonous Potato", "Ink Sac", "Glow Ink Sac", "Slimeball",
                    "Phantom Membrane",
                    "Rabbit Hide", "Rabbit Foot", "Pufferfish", "Nautilus Shell", "Granite", "Diorite", "Andesite",
                    "Tuff", "Calcite",
                    "Dripstone Block", "Pointed Dripstone", "Deepslate", "Cobbled Deepslate", "Basalt", "Blackstone",
                    "Netherrack",
                    "Soul Sand", "Soul Soil", "Magma Block", "Nether Bricks", "End Stone", "Dead Bush", "Fern",
                    "Large Fern", "Grass",
                    "Tall Grass", "Seeds", "Wheat Seeds", "Beetroot Seeds", "Kelp", "Seagrass", "Vines", "Sugar Cane",
                    "Bamboo", "Cactus",
                    "Lily Pad", "Azalea", "Flowering Azalea", "Azalea Leaves", "Flowering Azalea Leaves", "Moss Block",
                    "Moss Carpet", "Leaf Litter",
                    "Hanging Roots", "Rooted Dirt", "Small Dripleaf", "Big Dripleaf", "Spore Blossom", "Bowl",
                    "Glass Bottle", "Paper",
                    "Book", "Flint", "Clay Ball", "Charcoal", "Snowball", "Egg"
                };
                std::unordered_set<std::string> s;
                for (const char* i : items) s.insert(Lower(i));
                return s;
            }();
            return j;
        }

        const std::map<int, std::string>& VkNames()
        {
            static const std::map<int, std::string> m = {
                {0x2E, "Delete"}, {0x2D, "Insert"}, {0x23, "End"}, {0x24, "Home"}, {0x21, "PageUp"}, {0x22, "PageDown"},
                {0x70, "F1"}, {0x71, "F2"}, {0x72, "F3"}, {0x73, "F4"}, {0x74, "F5"}, {0x75, "F6"},
                {0x76, "F7"}, {0x77, "F8"}, {0x78, "F9"}, {0x79, "F10"}, {0x7A, "F11"}, {0x7B, "F12"},
                {0xC0, "Tilde"}, {0xDB, "["}, {0xDD, "]"}, {0xDC, "\\"}, {0xBA, ";"}, {0xDE, "'"},
                {0x20, "Space"}, {0x09, "Tab"}, {0x0D, "Enter"}, {0x1B, "Esc"}, {0x08, "Backspace"}, {0x25, "Left"}, {0x26, "Up"}, {0x27, "Right"}, {0x28, "Down"},
                {0x13, "Pause"}, {0x2C, "PrintScreen"}, {0x91, "ScrollLock"}, {0x90, "NumLock"}, {0x14, "CapsLock"},
                {0x60, "Numpad0"}, {0x61, "Numpad1"}, {0x62, "Numpad2"}, {0x63, "Numpad3"}, {0x64, "Numpad4"}, {0x65, "Numpad5"}, {0x66, "Numpad6"},
                {0x67, "Numpad7"}, {0x68, "Numpad8"}, {0x69, "Numpad9"}, {0x6A, "NumpadMultiply"}, {0x6B, "NumpadAdd"}, {0x6D, "NumpadSubtract"}, {0x6E, "NumpadDecimal"}, {0x6F, "NumpadDivide"},
                {0x7C, "F13"}, {0x7D, "F14"}, {0x7E, "F15"}, {0x7F, "F16"}, {0x80, "F17"}, {0x81, "F18"}, {0x82, "F19"}, {0x83, "F20"}, {0x84, "F21"}, {0x85, "F22"},
                {0x86, "F23"}, {0x87, "F24"},
                {0xBC, ","}, {0xBE, "."}, {0xBF, "/"}, {0xBD, "-"}, {0xBB, "="}
            };
            return m;
        }
    } // namespace

    bool IsJunkItem(const std::string& name) { return Junk().count(Lower(name)) != 0; }

    std::string VkToName(int vk)
    {
        auto it = VkNames().find(vk);
        if (it != VkNames().end()) return it->second;
        if ((vk >= 0x41 && vk <= 0x5A) || (vk >= 0x30 && vk <= 0x39)) return std::string(1, static_cast<char>(vk));
        char b[16];
        std::snprintf(b, sizeof b, "0x%02X", vk);
        return b;
    }

    int NameToVk(const std::string& name)
    {
        for (const auto& kv : VkNames())
            if (Lower(kv.second) == Lower(name)) return kv.first;
        if (name.size() == 1)
        {
            char c = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return c;
        }
        if (name.size() > 2 && name[0] == '0' && (name[1] == 'x' || name[1] == 'X'))
        {
            char* end = nullptr;
            long v = std::strtol(name.c_str() + 2, &end, 16);
            if (end && *end == 0) return static_cast<int>(v);
        }
        return 0x2E;
    }

    bool IsKnownKeyName(const std::string& name)
    {
        for (const auto& kv : VkNames())
            if (Lower(kv.second) == Lower(name)) return true;
        if (name.size() == 1)
        {
            const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
            return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        }
        if (name.size() > 2 && name[0] == '0' && (name[1] == 'x' || name[1] == 'X'))
        {
            char* end = nullptr;
            const long v = std::strtol(name.c_str() + 2, &end, 16);
            return end && *end == 0 && v > 0 && v < 256;
        }
        return false;
    }

    int NearestIdx(const std::vector<float>& a, float target)
    {
        int best = 0;
        float bd = std::fabs(a[0] - target);
        for (size_t i = 1; i < a.size(); ++i)
        {
            float d = std::fabs(a[i] - target);
            if (d < bd)
            {
                bd = d;
                best = static_cast<int>(i);
            }
        }
        return best;
    }

    int NearestIdx(const std::vector<int>& a, int target)
    {
        int best = 0, bd = std::abs(a[0] - target);
        for (size_t i = 1; i < a.size(); ++i)
        {
            int d = std::abs(a[i] - target);
            if (d < bd)
            {
                bd = d;
                best = static_cast<int>(i);
            }
        }
        return best;
    }
} // namespace presets
