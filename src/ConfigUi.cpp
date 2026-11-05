#include "ConfigUi.h"
#include "ItemFilter.h"
#include "Avatar.h"
#include "FriendList.h"
#include "Presets.h"
#include "ProfileManager.h"
#include "SkinCache.h"
#include <algorithm>
#include <cmath>

namespace
{
    // ---- layout (identical to the C# window) ----
    constexpr float kW = 480.f, kTabH = 24.f, kTitleH = 28.f, kRowH = 24.f, kSectH = 20.f, kPad = 10.f, kContentH =
                        400.f;
    constexpr float kValueCol = 300.f; // x offset of the value column
    const char* const kTabs[8] = {"GENERAL", "PLAYERS", "TRACERS", "ITEMS", "MINIMAP", "PROFILES", "HOTKEYS", "ADVANCED"};
    constexpr int kTabCount = 8;

    // ---- theme: deep slate with violet accent + teal values (the C# tool used a different palette) ----
    const Col kPanelBg{0.070f, 0.078f, 0.102f, 0.96f};
    const Col kPanelBorder{0.22f, 0.25f, 0.36f, 1.f};
    const Col kTitleBg{0.105f, 0.115f, 0.155f, 1.f};
    const Col kSectionBg{0.095f, 0.105f, 0.140f, 1.f};
    const Col kSectionText{0.36f, 0.86f, 0.78f, 1.f};
    const Col kAccent{0.46f, 0.37f, 1.00f, 1.f};
    const Col kTabIdle{0.085f, 0.093f, 0.125f, 1.f};
    const Col kHover{0.140f, 0.155f, 0.215f, 1.f};
    const Col kText{0.90f, 0.92f, 0.97f, 1.f};
    const Col kDim{0.55f, 0.59f, 0.69f, 1.f};
    const Col kValue{0.36f, 0.86f, 0.78f, 1.f};
    const Col kOff{0.34f, 0.37f, 0.46f, 1.f};
    const Col kButton{0.150f, 0.165f, 0.235f, 1.f};
    const Col kInputBg{0.050f, 0.056f, 0.078f, 1.f};
    const Col kWarn{1.00f, 0.55f, 0.35f, 1.f};
    const Col kRebind{0.95f, 0.62f, 0.20f, 1.f};
} // namespace

bool ConfigUi::Hot(float x, float y, float w, float h) const
{
    const float mx = in_->mx, my = in_->my;
    if (dragging_ || mx < x || mx > x + w || my < y || my > y + h) return false;
    if (clipOn_ && (mx < clipX_ || mx > clipX_ + clipW_ || my < clipY_ || my > clipY_ + clipH_)) return false;
    // scrolled out of view
    return true;
}

bool ConfigUi::NavStop(float x, float w, float h, UiNav::Act& act)
{
    act = UiNav::Act::None;
    const float contentY = y_ + scroll_[tab_] - (py_ + kTitleH + kTabH + kPad); // the same on every frame, whatever the scroll
    const bool focused = nav_.Stop(contentY, h);
    if (!focused) return false;
    act = nav_.Take();
    if (nav_.RingVisible()) g_->RRect(x - 1, y_ - 1, w + 2, h + 2, 5.f, kAccent, 1.5f);
    return true;
}

bool ConfigUi::CycleRow(const std::string& label, int& idx, int count, const std::string& value)
{
    const float lx = px_ + kPad, col = px_ + kValueCol, rowW = kW - 2 * kPad + 2;
    UiNav::Act act;
    NavStop(lx - 2, rowW, kRowH - 2, act);
    const bool hot = Hot(lx - 2, y_, rowW, kRowH);
    if (hot) g_->FillRRect(lx - 2, y_, rowW, kRowH - 2, 4.f, kHover);
    g_->Text(label, lx + 4, y_ + 4, Font::Ui, hot ? kSectionText : kText);
    g_->Text(value, col, y_ + 4, Font::Ui, kValue);
    bool changed = false;
    if (hot && in_->pressed)
    {
        idx = (idx + 1) % count;
        changed = true;
    } // left click: next preset
    else if (hot && in_->rpressed)
    {
        idx = (idx + count - 1) % count;
        changed = true;
    } // right click: previous
    else if (act == UiNav::Act::Next || act == UiNav::Act::Activate)
    {
        idx = (idx + 1) % count;
        changed = true;
    }
    else if (act == UiNav::Act::Prev)
    {
        idx = (idx + count - 1) % count;
        changed = true;
    }
    y_ += kRowH;
    if (changed) Save();
    return changed;
}

bool ConfigUi::ToggleRow(const std::string& label, bool& v)
{
    const float lx = px_ + kPad, col = px_ + kValueCol, rowW = kW - 2 * kPad + 2;
    UiNav::Act act;
    NavStop(lx - 2, rowW, kRowH - 2, act);
    const bool hot = Hot(lx - 2, y_, rowW, kRowH);
    if (hot) g_->FillRRect(lx - 2, y_, rowW, kRowH - 2, 4.f, kHover);
    g_->Text(label, lx + 4, y_ + 4, Font::Ui, hot ? kSectionText : kText);
    // toggle switch: pill + sliding knob
    const float sw = 32.f, sh = 14.f, sy = y_ + (kRowH - sh) * 0.5f - 1.f;
    g_->FillRRect(col, sy, sw, sh, sh * 0.5f, v ? kAccent : kOff);
    g_->FillCircle(v ? col + sw - sh * 0.5f : col + sh * 0.5f, sy + sh * 0.5f, sh * 0.5f - 2.f, Col{1, 1, 1, 1});
    g_->Text(v ? "ON" : "OFF", col + sw + 8, y_ + 4, Font::UiSmall, v ? kValue : kDim);
    bool changed = false;
    if (hot && in_->pressed)
    {
        v = !v;
        changed = true;
    }
    else if (act == UiNav::Act::Activate)
    {
        v = !v;
        changed = true;
    }
    else if ((act == UiNav::Act::Next && !v) || (act == UiNav::Act::Prev && v))
    {
        v = !v; // Right = on, Left = off
        changed = true;
    }
    y_ += kRowH;
    if (changed) Save();
    return changed;
}

void ConfigUi::InfoRow(const std::string& text, bool warn)
{
    g_->Text(text, px_ + kPad + 4, y_ + 2, Font::Hint, warn ? kWarn : kDim);
    y_ += 18.f;
}

void ConfigUi::Section(const std::string& title)
{
    g_->FillRect(px_ + 1, y_, kW - 2, kSectH, kSectionBg);
    g_->FillRect(px_ + 1, y_, 3, kSectH, kAccent);
    g_->Text(title, px_ + kPad + 2, y_ + 3, Font::UiBold, kSectionText);
    y_ += kSectH;
}

bool ConfigUi::Button(const std::string& label, float x, float w, float h, const Col* bg)
{
    UiNav::Act act;
    NavStop(x, w, h, act);
    const bool hot = Hot(x, y_, w, h);
    g_->FillRRect(x, y_, w, h, 4.f, hot ? kAccent : (bg ? *bg : kButton));
    g_->RRect(x, y_, w, h, 4.f, kPanelBorder, 1.f);
    g_->TextCentered(label, x + w * 0.5f, y_ + (h - 15.f) * 0.5f, Font::UiSmall, kText);
    return (hot && in_->pressed) || act == UiNav::Act::Activate;
}

bool ConfigUi::Input(const std::string& text, float x, float w, bool focused, const char* placeholder)
{
    UiNav::Act act;
    NavStop(x, w, 20.f, act);
    const bool hot = Hot(x, y_, w, 20.f);
    g_->FillRRect(x, y_, w, 20.f, 4.f, kInputBg);
    g_->RRect(x, y_, w, 20.f, 4.f, focused ? kAccent : kPanelBorder, 1.f);
    const bool caret = focused && ((frame_ / 30) % 2 == 0);
    if (text.empty() && !focused) g_->Text(placeholder, x + 6, y_ + 3, Font::UiSmall, kDim);
    else g_->Text(text + (caret ? "|" : ""), x + 6, y_ + 3, Font::UiSmall, kText);
    return (hot && in_->pressed) || act == UiNav::Act::Activate;
}

// ---------------------------------------------------------------------------------------------- tabs

void ConfigUi::TabGeneral()
{
    using namespace presets;
    CycleRow("Distance format", s_.distFmtIdx, 3, DistFormatLabels[s_.distFmtIdx]);
    {
        // the quick preset cycle; the ADVANCED tab has the slider. A distance between presets counts as the nearest one here.
        int idx = NearestIdx(LabelDist, s_.labelDist);
        const int before = idx;
        {
        int m = std::clamp(s_.hudMode, 0, 2);
        const int before = m;
        CycleRow("Status text (top-left)", m, 3, hud::Name(static_cast<hud::Mode>(m)));
        if (m != before) s_.hudMode = m;
    }
    CycleRow("Max label dist", idx, static_cast<int>(LabelDist.size()), LabelDistLabels[idx]);
        if (idx != before) s_.labelDist = LabelDist[idx];
    }
    ToggleRow("Text shadow", s_.textShadow);
    ToggleRow("Label declutter", s_.labelDeclutter);
    ToggleRow("Performance graphs", s_.showGraphs);
    CycleRow("Overlay opacity", s_.overlayOpacityIdx, static_cast<int>(OverlayOpacity.size()),
             OverlayOpacityLabels[s_.overlayOpacityIdx]);
    CycleRow("FPS cap", s_.fpsCap, static_cast<int>(FpsCaps.size()), FpsCapLabels[s_.fpsCap]);
    CycleRow("Camera FOV (fallback)", s_.fovIdx, static_cast<int>(Fovs.size()), FovLabels[s_.fovIdx]);
    ToggleRow("VSync", s_.vsync);
    ToggleRow("Motion smoothing", s_.smoothing);
    {
        static const char* const kViews[] = {"Smoothed", "Raw", "Both (raw = magenta)"};
        int v = std::clamp(s_.viewMode, 0, 2);
        const int before = v;
        CycleRow("View", v, 3, kViews[v]);
        if (v != before) s_.viewMode = v;
    }
    y_ += 4;
    if (!cameraInfo_.empty()) InfoRow(cameraInfo_);
    InfoRow("With the game camera found, its exact pose and FOV are used (sprint, underwater, third person).");
    InfoRow("The FOV setting is only a fallback.");
    if (diagnostics_)
    {
        y_ += 6;
        Section("SUPPORT");
        y_ += 4;
        if (Button("Copy diagnostics", px_ + kPad, 140)) diagnosticsNote_ = diagnostics_();
        y_ += 24;
        InfoRow(diagnosticsNote_.empty() ? "Copies a redacted report for a bug report; read it before posting." : diagnosticsNote_,
                !diagnosticsNote_.empty() && diagnosticsNote_.rfind("Could not", 0) == 0);
    }
}

void ConfigUi::TabPlayers()
{
    using namespace presets;
    ToggleRow("Show players", s_.showPlayers);
    ToggleRow("Player names", s_.showPlayerNames);
    CycleRow("Box style", s_.boxStyleIdx, 4, BoxStyleLabels[s_.boxStyleIdx]);
    CycleRow("Corner radius", s_.cornerRadiusIdx, static_cast<int>(CornerRadius.size()),
             CornerRadiusLabels[s_.cornerRadiusIdx]);
    CycleRow("Box thickness", s_.boxThickIdx, static_cast<int>(Thickness.size()), ThicknessLabels[s_.boxThickIdx]);
    CycleRow("Box color", s_.playerColorIdx, 2, BoxColorLabels[s_.playerColorIdx]);
    CycleRow("Name size", s_.nameSizeIdx, static_cast<int>(NameSizes.size()), NameSizeLabels[s_.nameSizeIdx]);
    CycleRow("Box fill opacity", s_.boxFillIdx, static_cast<int>(Opacities.size()), FillOpacityLabels[s_.boxFillIdx]);
    CycleRow("Max player dist", s_.maxPlayerDistIdx, static_cast<int>(PlayerDist.size()),
             PlayerDistLabels[s_.maxPlayerDistIdx]);
    ToggleRow("Health bars", s_.showHealth);

    // ---- friends ----
    y_ += 6;
    Section("FRIENDS");
    const float lx = px_ + kPad, inputW = kW - kPad * 2 - 68;
    auto submit = [&]()
    {
        if (FriendList::Add(friendInput_)) { friendInput_.clear(); }
        friendFocus_ = false;
    };
    if (Input(friendInput_, lx, inputW, friendFocus_, "friend name..."))
    {
        friendFocus_ = true;
        profileFocus_ = itemFocus_ = false;
    }
    if (Button("ADD", lx + inputW + 4, 60) && friendInput_.find_first_not_of(' ') != std::string::npos) submit();
    y_ += kRowH;
    if (friendFocus_)
    {
        for (int vk : in_->keys) if (vk == VK_RETURN && friendInput_.find_first_not_of(' ') != std::string::npos)
        {
            submit();
            break;
        }
    }

    for (const std::string& name : FriendList::Names())
    {
        const float a = 16.f;
        SkinCache::Request(name, ""); // real skin face (placeholder while loading)
        if (auto img = SkinCache::Get(name))
        {
            g_->DrawImage(*img, lx + 2, y_ + 2, a, a);
        }
        else
        {
            const AvatarColor ac = AvatarColorFor(name);
            g_->FillRRect(lx + 2, y_ + 2, a, a, 3.f, Col{ac.r, ac.g, ac.b, 0.95f});
            g_->TextCentered(AvatarInitial(name), lx + 2 + a * 0.5f, y_ + 2.f, Font::UiSmall, Col{1, 1, 1, 1});
        }
        g_->Text(name, lx + a + 10, y_ + 3, Font::Ui, Col{0.16f, 1.f, 0.35f, 1.f});
        if (Button("Remove", px_ + kW - 54 - kPad, 50)) { FriendList::Remove(name); }
        y_ += kRowH;
    }
    if (FriendList::Names().empty()) InfoRow("No friends yet. Friends are drawn green.");
}

void ConfigUi::TabTracers()
{
    using namespace presets;
    ToggleRow("Show tracers", s_.showTracers);
    CycleRow("Origin", s_.tracerOriginIdx, 3, TracerOriginLabels[std::clamp(s_.tracerOriginIdx, 0, 2)]);
    CycleRow("Max distance", s_.tracerMaxIdx, static_cast<int>(TracerDistLabels.size()), TracerDistLabels[static_cast<size_t>(std::clamp(s_.tracerMaxIdx, 0, 3))]);
    ToggleRow("To players", s_.tracerPlayers);
    CycleRow("Color", s_.tracerColorIdx, 2, TracerColorLabels[s_.tracerColorIdx]);
    CycleRow("Opacity", s_.tracerOpacityIdx, static_cast<int>(TraceOpacities.size()),
             TraceOpacityLabels[s_.tracerOpacityIdx]);
    CycleRow("Thickness", s_.tracerThickIdx, static_cast<int>(Thickness.size()), ThicknessLabels[s_.tracerThickIdx]);
    CycleRow("Target point", s_.tracerTargetIdx, 3, TracerTargetLabels[s_.tracerTargetIdx]);
    ToggleRow("Fade w/ distance", s_.tracerFade);
    {
        Section("TRACERS TO MOBS");
        static const char* const kNames[kEntityKindCount] = {"Other mobs", "Hostile mobs", "Passive mobs", "Neutral mobs", "Projectiles", "Experience orbs"};
        for (int i = 1; i <= kEntityKindCount; ++i)
        {
            const int kind = i % kEntityKindCount;
            ToggleRow(kNames[kind], s_.tracerEntity[kind]);
        }
        InfoRow("Drawn in the category's color; the mob must be shown (ITEMS tab, ENTITIES).");
    }
}

void ConfigUi::TabItems()
{
    using namespace presets;
    ToggleRow("Show items", s_.showItems);
    ToggleRow("Item names", s_.showItemNames);
    ToggleRow("Item count", s_.showItemCount);
    CycleRow("Item opacity", s_.itemOpacityIdx, static_cast<int>(TraceOpacities.size()),
             TraceOpacityLabels[s_.itemOpacityIdx]);
    ToggleRow("Item enchantments", s_.showItemEnchants);
    ToggleRow("Item tags (raw NBT)", s_.showItemTags);
    ToggleRow("Hide junk items", s_.hideJunk);

    // ---- filters (ItemFilter.h) ----
    y_ += 6;
    Section("ITEM FILTER");
    {
        int tier = std::clamp(s_.itemMinTier, 0, 3);
        const int beforeTier = tier;
        CycleRow("Minimum value", tier, 4, itemfilter::TierName(tier));
        if (tier != beforeTier) s_.itemMinTier = tier;
        int mode = std::clamp(s_.itemFilterMode, 0, 2);
        const int beforeMode = mode;
        CycleRow("Name filter", mode, 3, itemfilter::Name(static_cast<itemfilter::Mode>(mode)));
        if (mode != beforeMode) s_.itemFilterMode = mode;
    }
    {
        const float lx = px_ + kPad, inputW = kW - kPad * 2 - 68;
        auto submit = [&]()
        {
            s_.itemFilterList = itemfilter::AddPattern(s_.itemFilterList, itemInput_);
            itemInput_.clear();
            itemFocus_ = false;
            Save();
        };
        if (Input(itemInput_, lx, inputW, itemFocus_, "pattern, e.g. diamond*  or  *_sword"))
        {
            itemFocus_ = true;
            friendFocus_ = profileFocus_ = false;
        }
        const bool hasText = itemInput_.find_first_not_of(' ') != std::string::npos;
        if (Button("ADD", lx + inputW + 4, 60) && hasText) submit();
        y_ += kRowH;
        if (itemFocus_ && hasText)
            for (int vk : in_->keys) if (vk == VK_RETURN)
            {
                submit();
                break;
            }
        itemfilter::Rules shownRules;
        shownRules.Set(s_.itemFilterList);
        for (size_t i = 0; i < shownRules.Size(); ++i)
        {
            const std::string pat = shownRules.Pattern(i); // the copy: Remove changes the list under the loop
            g_->Text(pat, lx + 6, y_ + 3, Font::Ui, Col{0.80f, 0.85f, 1.f, 1.f});
            if (Button("Remove", px_ + kW - 54 - kPad, 50))
            {
                s_.itemFilterList = itemfilter::RemovePattern(s_.itemFilterList, pat);
                Save();
            }
            y_ += kRowH;
        }
        if (shownRules.Size() == 0) InfoRow("No patterns. Names ignore case, spaces and underscores; * matches any run.");
        else if (s_.itemFilterMode == 0) InfoRow("The pattern list is not used while \"Name filter\" is Off.");
    }
    y_ += 6;

    // ---- other entities (wire v6) ----
    Section("ENTITIES (MOBS, PROJECTILES, XP)");
    ToggleRow("Show entities", s_.showEntities);
    ToggleRow("Labels (name, distance)", s_.entLabels);
    {
        const int dn = static_cast<int>(presets::EntityDistLabels.size());
        CycleRow("Distance", s_.entDistIdx, dn, presets::EntityDistLabels[static_cast<size_t>(std::clamp(s_.entDistIdx, 0, dn - 1))]);
        static const char* const kNames[kEntityKindCount] = {"Other mobs", "Hostile mobs", "Passive mobs", "Neutral mobs", "Projectiles", "Experience orbs"};
        for (int i = 1; i <= kEntityKindCount; ++i)
        {
            const int kind = i % kEntityKindCount; // "Other mobs" last: hidden by default
            ToggleRow(kNames[kind], s_.entShow[kind]);
            const int cn = static_cast<int>(presets::EntityPaletteNames.size());
            CycleRow("    color", s_.entColor[kind], cn, presets::EntityPaletteNames[static_cast<size_t>(std::clamp(s_.entColor[kind], 0, cn - 1))]);
        }
    }
    InfoRow("Needs readable class names (docs/compatibility.md); the agent sends the 128 nearest.");
    y_ += 6;
    InfoRow("Items, enchantments and tags are read from the game by the agent (nearest 48).");
    InfoRow("Item frames / arrows (they also carry a stack) are filtered out by hitbox size.");
}

void ConfigUi::TabMinimap()
{
    using namespace presets;
    ToggleRow("Show minimap", s_.showMinimap);
    CycleRow("Size", s_.minimapSizeIdx, static_cast<int>(MinimapSizes.size()), MinimapSizeLabels[s_.minimapSizeIdx]);
    CycleRow("Range", s_.minimapRangeIdx, static_cast<int>(MinimapRanges.size()),
             MinimapRangeLabels[s_.minimapRangeIdx]);
    {
        static const char* const kModes[] = {"North up", "Rotate (you look up)"};
        static const char* const kCorners[] = {"Top right", "Top left", "Bottom right", "Bottom left"};
        CycleRow("Orientation", s_.minimapMode, 2, kModes[std::clamp(s_.minimapMode, 0, 1)]);
        CycleRow("Corner", s_.minimapCorner, 4, kCorners[std::clamp(s_.minimapCorner, 0, 3)]);
    }
    ToggleRow("Death marker", s_.showDeath);
    InfoRow("Where you died, on the radar and in the world, until you get there. Needs your health.");
}

void ConfigUi::TabProfiles()
{
    const float lx = px_ + kPad, inputW = kW - kPad * 2 - 78;
    Section("ACTIVE PROFILE");
    {
        const std::string active = ProfileManager::Active();
        g_->Text(active.empty() ? "(none - settings are your own)" : active, lx + 4, y_ + 3, Font::Ui, active.empty() ? kDim : kText);
        if (Button("NEXT", px_ + kW - 136 - kPad, 60, 18))
        {
            const std::string next = ProfileManager::CycleName(ProfileManager::ProfileNames(), active, +1);
            if (!next.empty()) ProfileManager::Activate(next, s_);
        }
        if (Button("UPDATE", px_ + kW - 70 - kPad, 64, 18) && !active.empty()) ProfileManager::SaveAs(active, s_.Build(), true); // overwrite the active profile with what is set now
        y_ += kRowH + 6;
    }
    Section("SAVE PROFILE");
    auto submit = [&]()
    {
        if (ProfileManager::Save(profileInput_, s_.Build())) profileInput_.clear();
        profileFocus_ = false;
    };
    if (Input(profileInput_, lx, inputW, profileFocus_, "profile name..."))
    {
        profileFocus_ = true;
        friendFocus_ = itemFocus_ = false;
    }
    if (Button("SAVE", lx + inputW + 4, 70) && profileInput_.find_first_not_of(' ') != std::string::npos) submit();
    if (profileFocus_)
        for (int vk : in_->keys) if (vk == VK_RETURN && profileInput_.find_first_not_of(' ') != std::string::npos)
        {
            submit();
            break;
        }
    y_ += kRowH + 6;

    Section("LOAD / DELETE");
    if (!ProfileManager::LastError().empty()) InfoRow(ProfileManager::LastError(), true);
    const std::vector<std::string> names = ProfileManager::ProfileNames(); // copy: Delete() refreshes the list
    if (names.empty())
    {
        g_->Text("No profiles saved yet.", lx + 4, y_ + 3, Font::Ui, kDim);
        y_ += kRowH;
    }
    for (const std::string& name : names)
    {
        g_->Text(name == ProfileManager::Active() ? name + "  (active)" : name, lx + 4, y_ + 3, Font::Ui, kText);
        if (Button("LOAD", px_ + kW - 120 - kPad, 54, 18))
        {
            ProfileManager::Activate(name, s_); // applies, marks active, asks for a save
        }
        if (Button("DEL", px_ + kW - 60 - kPad, 54, 18)) ProfileManager::Delete(name);
        y_ += kRowH;
    }
}

void ConfigUi::HotkeyRow(hotkeys::Action a)
{
    const hotkeys::ActionDef& d = hotkeys::Def(a);
    const hotkeys::Binding& cur = s_.hotkey[static_cast<size_t>(a)];
    const float col = px_ + kValueCol - 60.f;
    g_->Text(d.label, px_ + kPad + 4, y_ + 4, Font::Ui, kText);
    const int slot = static_cast<int>(a) + 1;
    const bool rebinding = rebindActive_ && rebindSlot_ == slot;
    const Col pulse = kRebind.with(0.65f + 0.35f * std::sin(static_cast<float>(frame_) * 0.15f));
    const std::string shown = rebinding ? "PRESS KEYS..." : (cur.Bound() ? hotkeys::Format(cur) : "(not set)");
    if (Button(shown, col, 160, 20, rebinding ? &pulse : nullptr))
    {
        rebindActive_ = true;
        rebindSlot_ = slot;
        friendFocus_ = profileFocus_ = itemFocus_ = false;
        hotkeyNote_.clear();
    }
    if (!d.required && cur.Bound() && !rebinding && Button("CLEAR", col + 168.f, 52, 20))
    {
        s_.hotkey[static_cast<size_t>(a)] = hotkeys::Binding{};
        hotkeyNote_.clear();
        Save();
    }
    y_ += kRowH;
}

void ConfigUi::TabHotkeys()
{
    Section("HOTKEYS  -  select a button, press the keys (Esc cancels)");
    for (int i = 0; i < hotkeys::kActionCount; ++i) HotkeyRow(hotkeys::ActionAt(i));
    y_ += 6;
    if (!hotkeyNote_.empty()) InfoRow(hotkeyNote_, true);
    if (hkRegistry_)
        for (int i = 0; i < hotkeys::kActionCount; ++i)
            if (const std::string& st = hkRegistry_->Status(hotkeys::ActionAt(i)); !st.empty()) InfoRow(st, true); // e.g. "Delete is already taken by another program ..."
    InfoRow("Hold Ctrl / Shift / Alt while you press the key to add it: Ctrl+Shift+F5.");
    InfoRow("Keys are taken from other programs only while Minecraft is focused.");
    InfoRow("Toggles change the setting itself, so they are saved like a click.");
}

bool ConfigUi::SliderRow(tunables::Id id)
{
    const tunables::Def& d = tunables::Get(id);
    const double v = tunables::Value(s_, id);
    const float lx = px_ + kPad, rowW = kW - 2 * kPad + 2;
    const float tx = px_ + 176.f, tw = 150.f; // the track
    UiNav::Act act;
    NavStop(lx - 2, rowW, kRowH - 2, act);
    const bool hotRow = Hot(lx - 2, y_, rowW, kRowH);
    if (hotRow) g_->FillRRect(lx - 2, y_, rowW, kRowH - 2, 4.f, kHover);
    g_->Text(d.label, lx + 4, y_ + 4, Font::Ui, hotRow ? kSectionText : kText);

    const double frac = std::clamp((v - d.min) / (d.max - d.min), 0.0, 1.0);
    const float cy = y_ + kRowH * 0.5f - 1.f;
    g_->FillRRect(tx, cy - 2.f, tw, 4.f, 2.f, kOff);
    g_->FillRRect(tx, cy - 2.f, std::max(4.f, tw * static_cast<float>(frac)), 4.f, 2.f, kAccent);
    g_->FillCircle(tx + tw * static_cast<float>(frac), cy, 6.f, Col{1, 1, 1, 1});
    g_->Text(tunables::Format(d, v), tx + tw + 14.f, y_ + 4, Font::Ui, tunables::IsDefault(s_, id) ? kValue : kRebind);

    bool changed = false;
    // mouse: press on the track starts a drag, which follows the cursor until the button is released (even outside the track)
    if (Hot(tx - 8.f, y_, tw + 16.f, kRowH) && in_->pressed) sliderDrag_ = static_cast<int>(id);
    if (sliderDrag_ == static_cast<int>(id))
    {
        if (in_->down)
        {
            const double f = std::clamp((in_->mx - tx) / tw, 0.f, 1.f);
            if (tunables::Set(s_, id, d.min + f * (d.max - d.min)) != v) changed = true;
        }
        else sliderDrag_ = -1;
    }
    // keyboard: Left / Right = one step, Delete = default
    if (act == UiNav::Act::Prev) changed = tunables::Set(s_, id, v - d.step) != v || changed;
    else if (act == UiNav::Act::Next) changed = tunables::Set(s_, id, v + d.step) != v || changed;
    else if (act == UiNav::Act::Reset && !tunables::IsDefault(s_, id))
    {
        tunables::Reset(s_, id);
        changed = true;
    }
    if (!tunables::IsDefault(s_, id) && Button("RESET", px_ + kW - kPad - 50.f, 46.f, 18.f))
    {
        tunables::Reset(s_, id);
        changed = true;
    }
    y_ += kRowH;
    InfoRow(d.help); // what it does, one line
    if (changed) Save();
    return changed;
}

void ConfigUi::TabAdvanced()
{
    using tunables::Id;
    Section("SMOOTHING  -  change one at a time");
    for (Id id : {Id::SmootherBaselineMs, Id::SmootherBlendTauMs, Id::SmootherMaxAheadMs, Id::SmootherTickMs}) SliderRow(id);
    InfoRow(tunables::DescribeError(liveStats_, liveH_, liveFov_)); // live: watch it while you move a slider
    InfoRow("Lower is better. It averages ~20 samples: wait a second after a change.");
    y_ += 6;
    Section("AGENT");
    SliderRow(Id::AgentHz);
    if (!rateNote_.empty()) InfoRow(rateNote_, rateWarn_);
    y_ += 6;
    Section("MARKERS AND LABELS");
    SliderRow(Id::ItemMinPx);
    SliderRow(Id::PlayerMinPx);
    SliderRow(Id::LabelDist);
    SliderRow(Id::LabelFadePct);
    y_ += 6;
    const float lx = px_ + kPad;
    bool any = false;
    for (int i = 0; i < tunables::Count(); ++i) any = any || !tunables::IsDefault(s_, static_cast<Id>(i));
    if (any)
    {
        if (Button("RESET ALL TO DEFAULTS", lx, 200.f, 20.f))
        {
            for (int i = 0; i < tunables::Count(); ++i) tunables::Reset(s_, static_cast<Id>(i));
            Save();
        }
        y_ += kRowH;
    }
    else InfoRow("Everything on this tab is at its default.");
}

// ---------------------------------------------------------------------------------------------- frame

void ConfigUi::Draw(Gfx& g, const UiInput& in, int winW, int winH)
{
    g_ = &g;
    in_ = &in;
    ++frame_;
    const float panelH = kTitleH + kTabH + kContentH;
    auto clampPos = [&]()
    {
        s_.cfgX = std::clamp(s_.cfgX, 0.f, std::max(0.f, winW - kW));
        s_.cfgY = std::clamp(s_.cfgY, 0.f, std::max(0.f, winH - panelH));
    };
    clampPos();

    // ---- drag the title bar ----
    if (dragging_)
    {
        if (in.down)
        {
            s_.cfgX = in.mx - dragOffX_;
            s_.cfgY = in.my - dragOffY_;
            clampPos();
        }
        else
        {
            dragging_ = false;
            Save();
        }
    }
    else if (in.pressed && in.mx >= s_.cfgX && in.mx <= s_.cfgX + kW && in.my >= s_.cfgY && in.my <= s_.cfgY + kTitleH)
    {
        dragging_ = true;
        dragOffX_ = in.mx - s_.cfgX;
        dragOffY_ = in.my - s_.cfgY;
    }
    px_ = s_.cfgX;
    py_ = s_.cfgY;

    // ---- text input / rebinding (consumes this frame's keys) ----
    // Whether a field or a rebind was waiting when the frame began: the keys of this frame (Esc, Enter, ...) belong to it even if it ends below.
    const bool capturedAtStart = CapturingKeys();
    if (in.pressed)
    {
        friendFocus_ = false;
        profileFocus_ = false;
        itemFocus_ = false;
    } // widgets re-focus themselves if clicked
    auto typing = [&](std::string& buf, size_t maxLen, bool focus)
    {
        if (!focus) return;
        for (char c : in.chars) if (c >= 32 && c < 127 && buf.size() < maxLen) buf += c;
        for (int vk : in.keys) if (vk == VK_BACK && !buf.empty()) buf.pop_back();
    };
    typing(friendInput_, 24, friendFocus_);
    typing(profileInput_, 32, profileFocus_);
    typing(itemInput_, itemfilter::kMaxPatternLen, itemFocus_);
    if ((friendFocus_ || profileFocus_ || itemFocus_)) for (int vk : in.keys) if (vk == VK_ESCAPE) friendFocus_ = profileFocus_ = itemFocus_ =
        false;
    if (rebindActive_)
    {
        for (int vk : in.keys)
        {
            if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_LSHIFT || vk == VK_RSHIFT ||
                vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_LMENU || vk == VK_RMENU || vk == VK_LWIN || vk == VK_RWIN)
                continue; // a modifier alone: wait for the key it goes with
            if (vk != VK_ESCAPE)
            {
                const hotkeys::Action a = hotkeys::ActionAt(rebindSlot_ - 1);
                hotkeys::Binding nb;
                nb.vk = vk;
                nb.mods = (in.ctrl ? hotkeys::kCtrl : 0u) | (in.shift ? hotkeys::kShift : 0u) | (in.alt ? hotkeys::kAlt : 0u) | (in.win ? hotkeys::kWin : 0u);
                const std::string problem = hotkeys::Problem(nb);
                const hotkeys::Action other = hotkeys::UsedBy(s_.hotkey, nb, a);
                if (!problem.empty()) hotkeyNote_ = hotkeys::Format(nb) + ": " + problem + ".";
                else if (other != hotkeys::Action::Count) hotkeyNote_ = hotkeys::Format(nb) + " is already used by \"" + hotkeys::Def(other).label + "\".";
                else
                {
                    s_.hotkey[static_cast<size_t>(a)] = nb;
                    hotkeyNote_.clear();
                    Save();
                }
            }
            rebindActive_ = false;
            break;
        }
    }

    // ---- keyboard navigation (not while a text field or a key rebind is waiting for input) ----
    if (in.mx != lastMx_ || in.my != lastMy_ || in.pressed || in.rpressed || in.wheel != 0.f) nav_.OnMouse();
    lastMx_ = in.mx;
    lastMy_ = in.my;
    nav_.BeginFrame(in.keys, in.shift, in.ctrl, capturedAtStart, stopsLast_[tab_]);
    if (const int d = nav_.TakeTabDelta(); d != 0) tab_ = ((tab_ + d) % kTabCount + kTabCount) % kTabCount;
    nav_.SetTab(tab_);
    if (nav_.CloseRequested()) closeRequested_ = true;

    // ---- panel chrome ----
    g.FillRRect(px_ + 2, py_ + 5, kW, panelH, 8.f, Col{0, 0, 0, 0.35f}); // drop shadow
    g.FillRRect(px_, py_, kW, panelH, 8.f, kPanelBg);
    g.FillRRect(px_, py_, kW, kTitleH + 4, 8.f, kTitleBg);
    g.FillRect(px_ + 1, py_ + kTitleH, kW - 2, 4, kPanelBg); // square off the title bar's lower corners
    g.FillRect(px_ + 12, py_ + kTitleH - 3, 62, 2, kAccent);
    g.Text("ESP CONFIG", px_ + 12, py_ + 6, Font::UiBold, kText);
    g.TextRight(nav_.RingVisible() ? "keys: arrows, Enter, Del = default, Ctrl+Tab, Esc" : "drag to move  |  right-click cycles backwards", px_ + kW - 10,
                py_ + 8, Font::Hint, kDim);

    // ---- tabs ----
    const float tabW = kW / static_cast<float>(kTabCount);
    for (int t = 0; t < kTabCount; ++t)
    {
        const float tx = px_ + t * tabW, ty = py_ + kTitleH;
        const bool hot = Hot(tx, ty, tabW, kTabH);
        g.FillRect(tx, ty, tabW, kTabH, tab_ == t ? kAccent : hot ? kHover : kTabIdle);
        g.TextCentered(kTabs[t], tx + tabW * 0.5f, ty + 5, Font::UiSmall, tab_ == t ? Col{1, 1, 1, 1} : kDim);
        if (hot && in.pressed) tab_ = t;
    }
    g.Rect(px_, py_, kW, panelH, kPanelBorder, 1.5f); // (drawn again after content below)

    // ---- scrolling content ----
    const float top = py_ + kTitleH + kTabH;
    const bool overPanel = in.mx >= px_ && in.mx <= px_ + kW && in.my >= py_ && in.my <= py_ + panelH;
    const float viewH = kContentH - kPad;
    const float maxScroll = std::max(0.f, contentH_[tab_] - viewH);
    if (overPanel && in.wheel != 0.f) scroll_[tab_] -= in.wheel * 28.f;
    scroll_[tab_] = std::clamp(scroll_[tab_], 0.f, maxScroll);

    g.PushClip(px_ + 1, top, kW - 2, kContentH - 1);
    clipOn_ = true;
    clipX_ = px_ + 1;
    clipY_ = top;
    clipW_ = kW - 2;
    clipH_ = kContentH - 1;
    y_ = top + kPad - scroll_[tab_];
    const float startY = y_;
    switch (tab_)
    {
    case 0: TabGeneral();
        break;
    case 1: TabPlayers();
        break;
    case 2: TabTracers();
        break;
    case 3: TabItems();
        break;
    case 4: TabMinimap();
        break;
    case 5: TabProfiles();
        break;
    case 6: TabHotkeys();
        break;
    case 7: TabAdvanced();
        break;
    }
    contentH_[tab_] = y_ - startY + kPad;
    stopsLast_[tab_] = nav_.StopCount();
    scroll_[tab_] = std::clamp(nav_.ScrollFor(scroll_[tab_], viewH), 0.f, std::max(0.f, contentH_[tab_] - viewH)); // applied from the next frame
    clipOn_ = false;
    g.PopClip();

    if (maxScroll > 0.f)
    {
        // scrollbar
        const float track = kContentH - 12.f, thumb = std::max(24.f, track * viewH / contentH_[tab_]);
        const float ty = top + 6.f + (track - thumb) * (scroll_[tab_] / maxScroll);
        g.FillRRect(px_ + kW - 7, ty, 3.f, thumb, 1.5f, kAccent.with(0.7f));
    }
}
