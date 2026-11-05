#include "EspConfig.h"
#include "ConfigSchema.h"
#include "ConfigStore.h"
#include "ItemFilter.h"
#include "Logger.h"
#include "Paths.h"
#include "Presets.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

using presets::kUnlimited;

namespace
{
    std::string Kv(const char* k, const std::string& raw, bool last = false)
    {
        return std::string("  \"") + k + "\": " + raw + (last ? "\n" : ",\n");
    }

    std::string S(const std::string& v) { return json::Escape(v); }
    std::string B(bool v) { return v ? "true" : "false"; }
    std::string N(double v) { return json::Number(v); }
} // namespace

std::string& EspConfig::HotkeyText(hotkeys::Action a)
{
    using hotkeys::Action;
    switch (a)
    {
    case Action::ToggleOverlay: return HotkeyToggle;
    case Action::OpenConfig: return HotkeyConfig;
    case Action::HoldExit: return HotkeyExit;
    case Action::TogglePlayers: return HotkeyTogglePlayers;
    case Action::ToggleItems: return HotkeyToggleItems;
    case Action::ToggleTracers: return HotkeyToggleTracers;
    case Action::ToggleMinimap: return HotkeyToggleMinimap;
    case Action::NextProfile: return HotkeyNextProfile;
    case Action::CycleHud: return HotkeyCycleHud;
    case Action::CycleView: return HotkeyCycleView;
    case Action::ToggleGraphs: return HotkeyToggleGraphs;
    case Action::ToggleEntities: return HotkeyToggleEntities;
    case Action::ToggleHostile: return HotkeyToggleHostile;
    case Action::TogglePassive: return HotkeyTogglePassive;
    case Action::ToggleNeutral: return HotkeyToggleNeutral;
    case Action::ToggleProjectiles: return HotkeyToggleProjectiles;
    case Action::ToggleXp: return HotkeyToggleXp;
    case Action::ClearDeathMarker: return HotkeyClearDeath;
    case Action::Count: break;
    }
    return HotkeyClearDeath; // not reachable for a real action
}

const EntityKeys& EntityKeysFor(int kind)
{
    static const EntityKeys k[kEntityKindCount] = {
        {"ShowOtherMobs", "ColorOtherMobs", "TracerOtherMobs"},   {"ShowHostile", "ColorHostile", "TracerHostile"},
        {"ShowPassive", "ColorPassive", "TracerPassive"},         {"ShowNeutral", "ColorNeutral", "TracerNeutral"},
        {"ShowProjectiles", "ColorProjectiles", "TracerProjectiles"}, {"ShowXp", "ColorXp", "TracerXp"}};
    return k[kind < 0 || kind >= kEntityKindCount ? 0 : kind];
}

std::string EspConfig::ToJson() const
{
    std::string j = "{\n";
    j += Kv("Version", std::to_string(configschema::kCurrentVersion));
    j += Kv("HotkeyToggle", S(HotkeyToggle)) + Kv("HotkeyConfig", S(HotkeyConfig)) + Kv("HotkeyExit", S(HotkeyExit));
    j += Kv("HotkeyTogglePlayers", S(HotkeyTogglePlayers)) + Kv("HotkeyToggleItems", S(HotkeyToggleItems)) +
        Kv("HotkeyToggleTracers", S(HotkeyToggleTracers)) + Kv("HotkeyToggleMinimap", S(HotkeyToggleMinimap)) + Kv("HotkeyNextProfile", S(HotkeyNextProfile)) +
        Kv("HotkeyCycleHud", S(HotkeyCycleHud)) + Kv("HotkeyCycleView", S(HotkeyCycleView)) + Kv("HotkeyToggleGraphs", S(HotkeyToggleGraphs)) +
        Kv("HotkeyToggleEntities", S(HotkeyToggleEntities)) + Kv("HotkeyToggleHostile", S(HotkeyToggleHostile)) + Kv("HotkeyTogglePassive", S(HotkeyTogglePassive)) +
        Kv("HotkeyToggleNeutral", S(HotkeyToggleNeutral)) + Kv("HotkeyToggleProjectiles", S(HotkeyToggleProjectiles)) + Kv("HotkeyToggleXp", S(HotkeyToggleXp)) +
        Kv("HotkeyClearDeath", S(HotkeyClearDeath));
    j += Kv("DistanceFormat", S(DistanceFormat)) + Kv("MaxLabelDist", N(MaxLabelDist)) +
        Kv("TextShadow", B(TextShadow));
    j += Kv("ShowPlayers", B(ShowPlayers)) + Kv("ShowPlayerNames", B(ShowPlayerNames)) + Kv("BoxStyle", S(BoxStyle)) +
        Kv("CornerRadius", N(CornerRadius)) + Kv("BoxThickness", N(BoxThickness)) + Kv("BoxColorMode", S(BoxColorMode))
        +
        Kv("NameSize", N(NameSize)) + Kv("BoxFillOpacity", N(BoxFillOpacity));
    j += Kv("ShowTracers", B(ShowTracers)) + Kv("TracerOrigin", S(TracerOrigin)) + Kv("TracerColor", S(TracerColor)) +
        Kv("TracerOpacity", N(TracerOpacity)) + Kv("TracerThickness", N(TracerThickness)) +
        Kv("TracerTarget", S(TracerTarget)) + Kv("TracerFade", B(TracerFade));
    j += Kv("ShowItems", B(ShowItems)) + Kv("ShowItemNames", B(ShowItemNames)) + Kv("ShowItemCount", B(ShowItemCount)) +
        Kv("ItemOpacity", N(ItemOpacity)) + Kv("HideJunk", B(HideJunk));
    j += Kv("MaxPlayerDist", N(MaxPlayerDist));
    j += Kv("ShowMinimap", B(ShowMinimap)) + Kv("MinimapSize", N(MinimapSize)) + Kv("MinimapRange", N(MinimapRange));
    j += Kv("OverlayOpacity", N(OverlayOpacity)) + Kv("FpsCap", N(FpsCap));
    j += Kv("ConfigX", N(ConfigX)) + Kv("ConfigY", N(ConfigY));
    j += Kv("Fov", N(Fov)) + Kv("VSync", B(VSync)) + Kv("Smoothing", B(Smoothing)) + Kv("ShowHealth", B(ShowHealth)) +
        Kv("ShowItemEnchants", B(ShowItemEnchants)) + Kv("ShowItemTags", B(ShowItemTags)) + Kv("HudMode", S(HudMode));
    j += Kv("ViewMode", S(ViewMode)) + Kv("ShowGraphs", B(ShowGraphs));
    j += Kv("TracerMaxDist", S(TracerMaxDist)) + Kv("TracerPlayers", B(TracerPlayers));
    j += Kv("MinimapMode", S(MinimapMode)) + Kv("MinimapCorner", S(MinimapCorner)) + Kv("ShowDeathMarker", B(ShowDeathMarker));
    j += Kv("ShowEntities", B(ShowEntities)) + Kv("EntityMaxDist", S(EntityMaxDist)) + Kv("EntityLabels", B(EntityLabels));
    for (int k = 0; k < kEntityKindCount; ++k) j += Kv(EntityKeysFor(k).show, B(EntityShow[k])) + Kv(EntityKeysFor(k).color, S(EntityColor[k])) + Kv(EntityKeysFor(k).tracer, B(TracerEntity[k]));
    j += Kv("ItemFilterMode", S(ItemFilterMode)) + Kv("ItemFilterList", S(ItemFilterList)) + Kv("ItemMinTier", N(ItemMinTier));
    j += Kv("LabelDeclutter", B(LabelDeclutter)) + Kv("LabelFadePct", N(LabelFadePct));
    j += Kv("SmootherBaselineMs", N(SmootherBaselineMs)) + Kv("SmootherBlendTauMs", N(SmootherBlendTauMs)) + Kv("SmootherMaxAheadMs", N(SmootherMaxAheadMs)) +
        Kv("SmootherTickMs", N(SmootherTickMs)) + Kv("AgentHz", N(AgentHz)) + Kv("ItemMinPx", N(ItemMinPx)) + Kv("PlayerMinPx", N(PlayerMinPx), Unknown.empty());
    size_t i = 0;
    for (const auto& kv : Unknown) j += "  " + json::Escape(kv.first) + ": " + kv.second + (++i == Unknown.size() ? "\n" : ",\n");
    return j + "}\n";
}

EspConfig EspConfig::FromJson(const json::Object& o)
{
    EspConfig c; // defaults for anything missing
    using namespace json;
    c.HotkeyToggle = GetString(o, "HotkeyToggle", c.HotkeyToggle);
    c.HotkeyConfig = GetString(o, "HotkeyConfig", c.HotkeyConfig);
    c.HotkeyExit = GetString(o, "HotkeyExit", c.HotkeyExit);
    c.HotkeyTogglePlayers = GetString(o, "HotkeyTogglePlayers", c.HotkeyTogglePlayers);
    c.HotkeyToggleItems = GetString(o, "HotkeyToggleItems", c.HotkeyToggleItems);
    c.HotkeyToggleTracers = GetString(o, "HotkeyToggleTracers", c.HotkeyToggleTracers);
    c.HotkeyToggleMinimap = GetString(o, "HotkeyToggleMinimap", c.HotkeyToggleMinimap);
    c.HotkeyNextProfile = GetString(o, "HotkeyNextProfile", c.HotkeyNextProfile);
    c.HotkeyCycleHud = GetString(o, "HotkeyCycleHud", c.HotkeyCycleHud);
    c.HotkeyCycleView = GetString(o, "HotkeyCycleView", c.HotkeyCycleView);
    c.HotkeyToggleGraphs = GetString(o, "HotkeyToggleGraphs", c.HotkeyToggleGraphs);
    c.HotkeyToggleEntities = GetString(o, "HotkeyToggleEntities", c.HotkeyToggleEntities);
    c.HotkeyToggleHostile = GetString(o, "HotkeyToggleHostile", c.HotkeyToggleHostile);
    c.HotkeyTogglePassive = GetString(o, "HotkeyTogglePassive", c.HotkeyTogglePassive);
    c.HotkeyToggleNeutral = GetString(o, "HotkeyToggleNeutral", c.HotkeyToggleNeutral);
    c.HotkeyToggleProjectiles = GetString(o, "HotkeyToggleProjectiles", c.HotkeyToggleProjectiles);
    c.HotkeyToggleXp = GetString(o, "HotkeyToggleXp", c.HotkeyToggleXp);
    c.HotkeyClearDeath = GetString(o, "HotkeyClearDeath", c.HotkeyClearDeath);
    c.HudMode = GetString(o, "HudMode", c.HudMode);
    c.ViewMode = GetString(o, "ViewMode", c.ViewMode);
    c.ShowGraphs = GetBool(o, "ShowGraphs", c.ShowGraphs);
    c.TracerMaxDist = GetString(o, "TracerMaxDist", c.TracerMaxDist);
    c.TracerPlayers = GetBool(o, "TracerPlayers", c.TracerPlayers);
    c.MinimapMode = GetString(o, "MinimapMode", c.MinimapMode);
    c.MinimapCorner = GetString(o, "MinimapCorner", c.MinimapCorner);
    c.ShowDeathMarker = GetBool(o, "ShowDeathMarker", c.ShowDeathMarker);
    c.ShowEntities = GetBool(o, "ShowEntities", c.ShowEntities);
    c.EntityMaxDist = GetString(o, "EntityMaxDist", c.EntityMaxDist);
    c.EntityLabels = GetBool(o, "EntityLabels", c.EntityLabels);
    for (int k = 0; k < kEntityKindCount; ++k)
    {
        c.EntityShow[k] = GetBool(o, EntityKeysFor(k).show, c.EntityShow[k]);
        c.EntityColor[k] = GetString(o, EntityKeysFor(k).color, c.EntityColor[k]);
        c.TracerEntity[k] = GetBool(o, EntityKeysFor(k).tracer, c.TracerEntity[k]);
    }
    c.ItemFilterMode = GetString(o, "ItemFilterMode", c.ItemFilterMode);
    c.ItemFilterList = GetString(o, "ItemFilterList", c.ItemFilterList);
    c.ItemMinTier = static_cast<int>(GetNumber(o, "ItemMinTier", c.ItemMinTier));
    c.LabelDeclutter = GetBool(o, "LabelDeclutter", c.LabelDeclutter);
    c.LabelFadePct = GetNumber(o, "LabelFadePct", c.LabelFadePct);
    c.DistanceFormat = GetString(o, "DistanceFormat", c.DistanceFormat);
    c.MaxLabelDist = static_cast<int>(GetNumber(o, "MaxLabelDist", c.MaxLabelDist));
    c.TextShadow = GetBool(o, "TextShadow", c.TextShadow);
    c.ShowPlayers = GetBool(o, "ShowPlayers", c.ShowPlayers);
    c.ShowPlayerNames = GetBool(o, "ShowPlayerNames", c.ShowPlayerNames);
    c.BoxStyle = GetString(o, "BoxStyle", c.BoxStyle);
    c.CornerRadius = static_cast<float>(GetNumber(o, "CornerRadius", c.CornerRadius));
    c.BoxThickness = static_cast<int>(GetNumber(o, "BoxThickness", c.BoxThickness));
    c.BoxColorMode = GetString(o, "BoxColorMode", c.BoxColorMode);
    c.NameSize = static_cast<int>(GetNumber(o, "NameSize", c.NameSize));
    c.BoxFillOpacity = static_cast<float>(GetNumber(o, "BoxFillOpacity", c.BoxFillOpacity));
    c.ShowTracers = GetBool(o, "ShowTracers", c.ShowTracers);
    c.TracerOrigin = GetString(o, "TracerOrigin", c.TracerOrigin);
    c.TracerColor = GetString(o, "TracerColor", c.TracerColor);
    c.TracerOpacity = static_cast<float>(GetNumber(o, "TracerOpacity", c.TracerOpacity));
    c.TracerThickness = static_cast<int>(GetNumber(o, "TracerThickness", c.TracerThickness));
    c.TracerTarget = GetString(o, "TracerTarget", c.TracerTarget);
    c.TracerFade = GetBool(o, "TracerFade", c.TracerFade);
    c.ShowItems = GetBool(o, "ShowItems", c.ShowItems);
    c.ShowItemNames = GetBool(o, "ShowItemNames", c.ShowItemNames);
    c.ShowItemCount = GetBool(o, "ShowItemCount", c.ShowItemCount);
    c.ItemOpacity = static_cast<float>(GetNumber(o, "ItemOpacity", c.ItemOpacity));
    c.HideJunk = GetBool(o, "HideJunk", c.HideJunk);
    c.MaxPlayerDist = static_cast<int>(GetNumber(o, "MaxPlayerDist", c.MaxPlayerDist));
    c.ShowMinimap = GetBool(o, "ShowMinimap", c.ShowMinimap);
    c.MinimapSize = static_cast<int>(GetNumber(o, "MinimapSize", c.MinimapSize));
    c.MinimapRange = static_cast<int>(GetNumber(o, "MinimapRange", c.MinimapRange));
    c.OverlayOpacity = static_cast<float>(GetNumber(o, "OverlayOpacity", c.OverlayOpacity));
    c.FpsCap = static_cast<int>(GetNumber(o, "FpsCap", c.FpsCap));
    c.ConfigX = static_cast<float>(GetNumber(o, "ConfigX", c.ConfigX));
    c.ConfigY = static_cast<float>(GetNumber(o, "ConfigY", c.ConfigY));
    c.Fov = static_cast<int>(GetNumber(o, "Fov", c.Fov));
    c.VSync = GetBool(o, "VSync", c.VSync);
    c.Smoothing = GetBool(o, "Smoothing", c.Smoothing);
    c.ShowHealth = GetBool(o, "ShowHealth", c.ShowHealth);
    c.ShowItemEnchants = GetBool(o, "ShowItemEnchants", c.ShowItemEnchants);
    c.ShowItemTags = GetBool(o, "ShowItemTags", c.ShowItemTags);
    c.SmootherBaselineMs = GetNumber(o, "SmootherBaselineMs", c.SmootherBaselineMs);
    c.SmootherBlendTauMs = GetNumber(o, "SmootherBlendTauMs", c.SmootherBlendTauMs);
    c.SmootherMaxAheadMs = GetNumber(o, "SmootherMaxAheadMs", c.SmootherMaxAheadMs);
    c.SmootherTickMs = GetNumber(o, "SmootherTickMs", c.SmootherTickMs);
    c.AgentHz = static_cast<int>(GetNumber(o, "AgentHz", c.AgentHz));
    c.ItemMinPx = GetNumber(o, "ItemMinPx", c.ItemMinPx);
    c.PlayerMinPx = GetNumber(o, "PlayerMinPx", c.PlayerMinPx);
    for (const auto& kv : o)
        if (!configschema::IsKnownKey(kv.first) && !kv.second.raw.empty()) c.Unknown[kv.first] = kv.second.raw;
    return c;
}

// ---- Settings <-> EspConfig (ApplyConfig / BuildConfig in the C# tool) ------------------------------

void Settings::Apply(const EspConfig& c)
{
    using namespace presets;
    {
        EspConfig tmp = c; // HotkeyText is non-const
        for (int i = 0; i < hotkeys::kActionCount; ++i)
            if (!hotkeys::Parse(tmp.HotkeyText(hotkeys::ActionAt(i)), hotkey[static_cast<size_t>(i)])) // the schema has reported it; here: the default
                hotkeys::Parse(hotkeys::Def(hotkeys::ActionAt(i)).defaultText, hotkey[static_cast<size_t>(i)]);
    }

    distFmtIdx = c.DistanceFormat == "Kilometers" ? 1 : c.DistanceFormat == "Off" ? 2 : 0;
    labelDist = c.MaxLabelDist < 0 ? kUnlimited : static_cast<float>(c.MaxLabelDist); // any distance now, not only the old presets
    textShadow = c.TextShadow;

    showPlayers = c.ShowPlayers;
    showPlayerNames = c.ShowPlayerNames;
    boxStyleIdx = c.BoxStyle == "Rounded" ? 0 : c.BoxStyle == "Corner-only" ? 2 : c.BoxStyle == "Skeleton" ? 3 : 1;
    cornerRadiusIdx = NearestIdx(CornerRadius, c.CornerRadius);
    boxThickIdx = NearestIdx(Thickness, static_cast<float>(c.BoxThickness));
    playerColorIdx = c.BoxColorMode == "Gradient" ? 1 : 0;
    nameSizeIdx = NearestIdx(NameSizes, static_cast<float>(c.NameSize));
    boxFillIdx = NearestIdx(Opacities, c.BoxFillOpacity);

    showTracers = c.ShowTracers;
    tracerOriginIdx = c.TracerOrigin == "Center" ? 1 : c.TracerOrigin == "Top" ? 2 : 0;
    tracerPlayers = c.TracerPlayers;
    tracerMaxIdx = 3;
    for (size_t i = 0; i < presets::TracerDistLabels.size(); ++i)
        if (presets::TracerDistLabels[i] == c.TracerMaxDist) tracerMaxIdx = static_cast<int>(i);
    for (int k = 0; k < kEntityKindCount; ++k) tracerEntity[k] = c.TracerEntity[k];
    tracerColorIdx = c.TracerColor == "White" ? 1 : 0;
    tracerOpacityIdx = NearestIdx(TraceOpacities, c.TracerOpacity);
    tracerThickIdx = NearestIdx(Thickness, static_cast<float>(c.TracerThickness));
    tracerTargetIdx = c.TracerTarget == "Feet" ? 0 : c.TracerTarget == "Head" ? 2 : 1;
    tracerFade = c.TracerFade;

    showItems = c.ShowItems;
    showItemNames = c.ShowItemNames;
    showItemCount = c.ShowItemCount;
    itemOpacityIdx = NearestIdx(TraceOpacities, c.ItemOpacity);
    hideJunk = c.HideJunk;

    maxPlayerDistIdx = NearestIdx(PlayerDist, c.MaxPlayerDist < 0 ? kUnlimited : static_cast<float>(c.MaxPlayerDist));

    showMinimap = c.ShowMinimap;
    minimapSizeIdx = NearestIdx(MinimapSizes, c.MinimapSize);
    minimapRangeIdx = NearestIdx(MinimapRanges, c.MinimapRange);

    overlayOpacityIdx = NearestIdx(OverlayOpacity, c.OverlayOpacity);
    fpsCap = NearestIdx(FpsCaps, c.FpsCap);

    cfgX = c.ConfigX;
    cfgY = c.ConfigY;

    fovIdx = NearestIdx(Fovs, c.Fov);
    vsync = c.VSync;
    smoothing = c.Smoothing;
    showHealth = c.ShowHealth;
    showItemEnchants = c.ShowItemEnchants;
    showItemTags = c.ShowItemTags;
    smBaselineMs = c.SmootherBaselineMs;
    smBlendTauMs = c.SmootherBlendTauMs;
    smMaxAheadMs = c.SmootherMaxAheadMs;
    smTickMs = c.SmootherTickMs;
    agentHz = c.AgentHz;
    {
        hud::Mode m = hud::Mode::Compact;
        hud::Parse(c.HudMode, m);
        hudMode = static_cast<int>(m);
    }
    {
        itemfilter::Mode m = itemfilter::Mode::Off;
        itemfilter::Parse(c.ItemFilterMode, m);
        itemFilterMode = static_cast<int>(m);
    }
    itemFilterList = c.ItemFilterList;
    showGraphs = c.ShowGraphs;
    minimapMode = c.MinimapMode == "Rotate" ? 1 : 0;
    minimapCorner = c.MinimapCorner == "TopLeft" ? 1 : c.MinimapCorner == "BottomRight" ? 2 : c.MinimapCorner == "BottomLeft" ? 3 : 0;
    showDeath = c.ShowDeathMarker;
    showEntities = c.ShowEntities;
    entLabels = c.EntityLabels;
    {
        const EspConfig defaults;
        entDistIdx = 1;
        for (size_t i = 0; i < presets::EntityDistLabels.size(); ++i)
            if (presets::EntityDistLabels[i] == c.EntityMaxDist) entDistIdx = static_cast<int>(i);
        for (int k = 0; k < kEntityKindCount; ++k)
        {
            entShow[k] = c.EntityShow[k];
            entColor[k] = presets::EntityPaletteIndex(c.EntityColor[k], presets::EntityPaletteIndex(defaults.EntityColor[k], 7));
        }
    }
    viewMode = c.ViewMode == "Raw" ? 1 : c.ViewMode == "Both" ? 2 : 0;
    itemMinTier = std::clamp(c.ItemMinTier, 0, 3);
    labelDeclutter = c.LabelDeclutter;
    labelFadePct = static_cast<int>(std::lround(c.LabelFadePct));
    itemMinPx = static_cast<float>(c.ItemMinPx);
    playerMinPx = static_cast<float>(c.PlayerMinPx);
    unknownKeys = c.Unknown;
}

EspConfig Settings::Build() const
{
    using namespace presets;
    EspConfig c;
    for (int i = 0; i < hotkeys::kActionCount; ++i) c.HotkeyText(hotkeys::ActionAt(i)) = hotkeys::Format(hotkey[static_cast<size_t>(i)]);
    c.DistanceFormat = DistFormatLabels[distFmtIdx];
    c.MaxLabelDist = labelDist >= kUnlimited ? -1 : static_cast<int>(labelDist);
    c.TextShadow = textShadow;
    c.ShowPlayers = showPlayers;
    c.ShowPlayerNames = showPlayerNames;
    c.BoxStyle = BoxStyleLabels[boxStyleIdx];
    c.CornerRadius = CornerRadius[cornerRadiusIdx];
    c.BoxThickness = static_cast<int>(Thickness[boxThickIdx]);
    c.BoxColorMode = playerColorIdx == 1 ? "Gradient" : "Static";
    c.NameSize = static_cast<int>(NameSizes[nameSizeIdx]);
    c.BoxFillOpacity = Opacities[boxFillIdx];
    c.ShowTracers = showTracers;
    c.TracerOrigin = tracerOriginIdx == 1 ? "Center" : tracerOriginIdx == 2 ? "Top" : "Bottom";
    c.TracerPlayers = tracerPlayers;
    c.TracerMaxDist = presets::TracerDistLabels[static_cast<size_t>(std::clamp(tracerMaxIdx, 0, static_cast<int>(presets::TracerDistLabels.size()) - 1))];
    for (int k = 0; k < kEntityKindCount; ++k) c.TracerEntity[k] = tracerEntity[k];
    c.TracerColor = tracerColorIdx == 1 ? "White" : "Dynamic";
    c.TracerOpacity = TraceOpacities[tracerOpacityIdx];
    c.TracerThickness = static_cast<int>(Thickness[tracerThickIdx]);
    c.TracerTarget = TracerTargetLabels[tracerTargetIdx];
    c.TracerFade = tracerFade;
    c.ShowItems = showItems;
    c.ShowItemNames = showItemNames;
    c.ShowItemCount = showItemCount;
    c.ItemOpacity = TraceOpacities[itemOpacityIdx];
    c.HideJunk = hideJunk;
    c.MaxPlayerDist = PlayerDist[maxPlayerDistIdx] == kUnlimited ? -1 : static_cast<int>(PlayerDist[maxPlayerDistIdx]);
    c.ShowMinimap = showMinimap;
    c.MinimapSize = MinimapSizes[minimapSizeIdx];
    c.MinimapRange = MinimapRanges[minimapRangeIdx];
    c.OverlayOpacity = OverlayOpacity[overlayOpacityIdx];
    c.FpsCap = FpsCaps[fpsCap];
    c.ConfigX = cfgX;
    c.ConfigY = cfgY;
    c.Fov = Fovs[fovIdx];
    c.VSync = vsync;
    c.Smoothing = smoothing;
    c.ShowHealth = showHealth;
    c.ShowItemEnchants = showItemEnchants;
    c.ShowItemTags = showItemTags;
    c.SmootherBaselineMs = smBaselineMs;
    c.SmootherBlendTauMs = smBlendTauMs;
    c.SmootherMaxAheadMs = smMaxAheadMs;
    c.SmootherTickMs = smTickMs;
    c.AgentHz = agentHz;
    c.HudMode = hud::Name(static_cast<hud::Mode>(hudMode));
    c.ItemFilterMode = itemfilter::Name(static_cast<itemfilter::Mode>(itemFilterMode));
    c.ItemFilterList = itemFilterList;
    c.ShowGraphs = showGraphs;
    c.MinimapMode = minimapMode == 1 ? "Rotate" : "North";
    c.MinimapCorner = minimapCorner == 1 ? "TopLeft" : minimapCorner == 2 ? "BottomRight" : minimapCorner == 3 ? "BottomLeft" : "TopRight";
    c.ShowDeathMarker = showDeath;
    c.ShowEntities = showEntities;
    c.EntityLabels = entLabels;
    c.EntityMaxDist = presets::EntityDistLabels[static_cast<size_t>(std::clamp(entDistIdx, 0, static_cast<int>(presets::EntityDistLabels.size()) - 1))];
    for (int k = 0; k < kEntityKindCount; ++k)
    {
        c.EntityShow[k] = entShow[k];
        c.EntityColor[k] = presets::EntityPaletteNames[static_cast<size_t>(std::clamp(entColor[k], 0, static_cast<int>(presets::EntityPaletteNames.size()) - 1))];
    }
    c.ViewMode = viewMode == 1 ? "Raw" : viewMode == 2 ? "Both" : "Smoothed";
    c.ItemMinTier = itemMinTier;
    c.LabelDeclutter = labelDeclutter;
    c.LabelFadePct = labelFadePct;
    c.ItemMinPx = itemMinPx;
    c.PlayerMinPx = playerMinPx;
    c.Unknown = unknownKeys;
    return c;
}

std::filesystem::path Settings::ConfigPath() { return ConfigDir() / "esp_config.json"; }

void Settings::Load()
{
    const auto path = ConfigPath();
    loadNotes.clear();
    const configstore::ReadResult rr = configstore::Read(path, configstore::IsJsonObject);
    switch (rr.status)
    {
    case configstore::ReadStatus::Missing:
        LOG_INFO("LoadConfig: esp_config.json not found - using defaults.");
        return;
    case configstore::ReadStatus::Unreadable:
        LOG_ERROR("LoadConfig: %s", rr.note.c_str());
        loadNotes.push_back(rr.note);
        return;
    case configstore::ReadStatus::Damaged:
        LOG_ERROR("LoadConfig: %s", rr.note.c_str());
        loadNotes.push_back(rr.note);
        return;
    case configstore::ReadStatus::RestoredFromBackup:
        LOG_WARN("LoadConfig: %s", rr.note.c_str());
        loadNotes.push_back(rr.note);
        break;
    case configstore::ReadStatus::Ok: break;
    }
    const configschema::LoadResult r = configschema::LoadText(rr.text);
    for (const auto& i : r.issues)
    {
        if (i.level == configschema::Issue::Level::Warning)
        {
            loadNotes.push_back(i.message);
            LOG_WARN("LoadConfig: %s", i.message.c_str());
        }
        else LOG_INFO("LoadConfig: %s", i.message.c_str());
    }
    Apply(r.cfg);
    LOG_INFO("LoadConfig: config loaded (%s).", r.Summary().c_str());
}

int Settings::StoredAgentHz()
{
    const configstore::ReadResult rr = configstore::Read(ConfigPath(), configstore::IsJsonObject);
    if (rr.text.empty()) return 0;
    return configschema::LoadText(rr.text).cfg.AgentHz;
}

bool Settings::Save() const
{
    std::string err;
    configstore::WriteOptions o;
    o.validPrevious = configstore::IsJsonObject;
    if (!configstore::WriteAtomic(ConfigPath(), Build().ToJson(), &err, o))
    {
        LOG_ERROR("SaveConfig: %s", err.c_str());
        return false;
    }
    return true;
}

void Settings::RequestSave()
{
    if (!saveDirty_) saveRequestedAt_ = std::chrono::steady_clock::now();
    saveDirty_ = true;
    lastChangeAt_ = std::chrono::steady_clock::now();
}

bool Settings::SaveIfDue(std::chrono::steady_clock::time_point now, std::chrono::milliseconds quiet)
{
    // write when the changes have stopped for `quiet` (a slider being dragged is one save, not sixty), but never keep a change unsaved for over 3 s
    if (!saveDirty_) return false;
    if (now - lastChangeAt_ < quiet && now - saveRequestedAt_ < std::chrono::seconds(3)) return false;
    return Flush();
}

bool Settings::Flush()
{
    if (!saveDirty_) return true;
    saveDirty_ = false;
    return Save();
}
