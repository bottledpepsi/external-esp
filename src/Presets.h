// Presets.h - port of Presets.cs: enums, preset tables (value + label), junk items, VK names.
#pragma once
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

enum class BoxStyle { Rounded, Sharp, CornerOnly, Skeleton };

enum class BoxColorMode { Static, Gradient };

enum class DistanceFormat { Meters, Kilometers, Off };

namespace presets
{
    inline constexpr float kUnlimited = std::numeric_limits<float>::max();

    extern const std::vector<float> Opacities;
    extern const std::vector<std::string> FillOpacityLabels;
    extern const std::vector<float> TraceOpacities;
    extern const std::vector<std::string> TraceOpacityLabels;
    extern const std::vector<float> CornerRadius;
    extern const std::vector<std::string> CornerRadiusLabels;
    extern const std::vector<float> Thickness;
    extern const std::vector<std::string> ThicknessLabels;
    extern const std::vector<float> NameSizes;
    extern const std::vector<std::string> NameSizeLabels;
    extern const std::vector<float> LabelDist;
    extern const std::vector<std::string> LabelDistLabels;
    extern const std::vector<float> PlayerDist;
    extern const std::vector<std::string> PlayerDistLabels;
    extern const std::vector<float> OverlayOpacity;
    extern const std::vector<std::string> OverlayOpacityLabels;
    extern const std::vector<int> FpsCaps;
    extern const std::vector<std::string> FpsCapLabels;
    extern const std::vector<int> MinimapSizes;
    extern const std::vector<std::string> MinimapSizeLabels;
    extern const std::vector<int> MinimapRanges;
    extern const std::vector<std::string> MinimapRangeLabels;
    extern const std::vector<int> Fovs;
    extern const std::vector<std::string> FovLabels; // addition: camera FOV

    extern const std::vector<std::string> DistFormatLabels; // Meters / Kilometers / Off
    extern const std::vector<std::string> BoxStyleLabels; // Rounded / Sharp / Corner-only / Skeleton
    extern const std::vector<std::string> BoxColorLabels; // Static / Gradient
    extern const std::vector<std::string> TracerOriginLabels; // Bottom Center / Screen Center
    extern const std::vector<std::string> TracerColorLabels; // Dynamic / White
    extern const std::vector<std::string> TracerTargetLabels; // Feet / Center / Head

    bool IsJunkItem(const std::string& name); // case-insensitive

    std::string VkToName(int vk);
    int NameToVk(const std::string& name); // unknown -> 0x2E (Delete), like the C# tool
    bool IsKnownKeyName(const std::string& name); // true when NameToVk(name) understands it (a table name, one letter/digit, or 0xNN)

    int NearestIdx(const std::vector<float>& arr, float target);
    int NearestIdx(const std::vector<int>& arr, int target);
} // namespace presets
