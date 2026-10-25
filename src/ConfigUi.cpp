#include "ConfigUi.h"
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
    const char* const kTabs[7] = {"GENERAL", "PLAYERS", "TRACERS", "ITEMS", "MINIMAP", "PROFILES", "HOTKEYS"};

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

bool ConfigUi::CycleRow(const std::string& label, int& idx, int count, const std::string& value)
{
    const float lx = px_ + kPad, col = px_ + kValueCol, rowW = kW - 2 * kPad + 2;
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
    y_ += kRowH;
    if (changed) Save();
    return changed;
}

bool ConfigUi::ToggleRow(const std::string& label, bool& v)
{
    const float lx = px_ + kPad, col = px_ + kValueCol, rowW = kW - 2 * kPad + 2;
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
    const bool hot = Hot(x, y_, w, h);
    g_->FillRRect(x, y_, w, h, 4.f, hot ? kAccent : (bg ? *bg : kButton));
    g_->RRect(x, y_, w, h, 4.f, kPanelBorder, 1.f);
    g_->TextCentered(label, x + w * 0.5f, y_ + (h - 15.f) * 0.5f, Font::UiSmall, kText);
    return hot && in_->pressed;
}

bool ConfigUi::Input(const std::string& text, float x, float w, bool focused, const char* placeholder)
{
    const bool hot = Hot(x, y_, w, 20.f);
    g_->FillRRect(x, y_, w, 20.f, 4.f, kInputBg);
    g_->RRect(x, y_, w, 20.f, 4.f, focused ? kAccent : kPanelBorder, 1.f);
    const bool caret = focused && ((frame_ / 30) % 2 == 0);
    if (text.empty() && !focused) g_->Text(placeholder, x + 6, y_ + 3, Font::UiSmall, kDim);
    else g_->Text(text + (caret ? "|" : ""), x + 6, y_ + 3, Font::UiSmall, kText);
    return hot && in_->pressed;
}

// ---------------------------------------------------------------------------------------------- tabs

void ConfigUi::TabGeneral()
{
    using namespace presets;
    CycleRow("Distance format", s_.distFmtIdx, 3, DistFormatLabels[s_.distFmtIdx]);
    CycleRow("Max label dist", s_.labelDistIdx, static_cast<int>(LabelDist.size()), LabelDistLabels[s_.labelDistIdx]);
    ToggleRow("Text shadow", s_.textShadow);
    CycleRow("Overlay opacity", s_.overlayOpacityIdx, static_cast<int>(OverlayOpacity.size()),
             OverlayOpacityLabels[s_.overlayOpacityIdx]);
    CycleRow("FPS cap", s_.fpsCap, static_cast<int>(FpsCaps.size()), FpsCapLabels[s_.fpsCap]);
    CycleRow("Camera FOV (fallback)", s_.fovIdx, static_cast<int>(Fovs.size()), FovLabels[s_.fovIdx]);
    ToggleRow("VSync", s_.vsync);
    ToggleRow("Motion smoothing", s_.smoothing);
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
        profileFocus_ = false;
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
    CycleRow("Origin", s_.tracerOriginIdx, 2, TracerOriginLabels[s_.tracerOriginIdx]);
    CycleRow("Color", s_.tracerColorIdx, 2, TracerColorLabels[s_.tracerColorIdx]);
    CycleRow("Opacity", s_.tracerOpacityIdx, static_cast<int>(TraceOpacities.size()),
             TraceOpacityLabels[s_.tracerOpacityIdx]);
    CycleRow("Thickness", s_.tracerThickIdx, static_cast<int>(Thickness.size()), ThicknessLabels[s_.tracerThickIdx]);
    CycleRow("Target point", s_.tracerTargetIdx, 3, TracerTargetLabels[s_.tracerTargetIdx]);
    ToggleRow("Fade w/ distance", s_.tracerFade);
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
}

void ConfigUi::TabProfiles()
{
    const float lx = px_ + kPad, inputW = kW - kPad * 2 - 78;
    Section("SAVE PROFILE");
    auto submit = [&]()
    {
        if (ProfileManager::Save(profileInput_, s_.Build())) profileInput_.clear();
        profileFocus_ = false;
    };
    if (Input(profileInput_, lx, inputW, profileFocus_, "profile name..."))
    {
        profileFocus_ = true;
        friendFocus_ = false;
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
        g_->Text(name, lx + 4, y_ + 3, Font::Ui, kText);
        if (Button("LOAD", px_ + kW - 120 - kPad, 54, 18))
        {
            if (auto cfg = ProfileManager::Load(name))
            {
                s_.Apply(*cfg);
                Save();
            }
        }
        if (Button("DEL", px_ + kW - 60 - kPad, 54, 18)) ProfileManager::Delete(name);
        y_ += kRowH;
    }
}

void ConfigUi::HotkeyRow(const std::string& label, int& vk, int slot)
{
    const float col = px_ + kValueCol;
    g_->Text(label, px_ + kPad + 4, y_ + 4, Font::Ui, kText);
    const bool rebinding = rebindActive_ && rebindSlot_ == slot;
    const Col pulse = kRebind.with(0.65f + 0.35f * std::sin(static_cast<float>(frame_) * 0.15f));
    if (Button(rebinding ? "PRESS KEY..." : presets::VkToName(vk), col, 120, 20, rebinding ? &pulse : nullptr))
    {
        rebindActive_ = true;
        rebindSlot_ = slot;
        friendFocus_ = profileFocus_ = false;
    }
    y_ += kRowH;
}

void ConfigUi::TabHotkeys()
{
    Section("HOTKEYS  -  click a button, then press the new key (Esc cancels)");
    HotkeyRow("Toggle overlay", s_.vkToggle, 1);
    HotkeyRow("Open config", s_.vkConfig, 2);
    HotkeyRow("Hold to exit (3 s)", s_.vkExit, 3);
    y_ += 6;
    InfoRow("Hotkeys work while Minecraft is focused (they are polled globally).");
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
    if (in.pressed)
    {
        friendFocus_ = false;
        profileFocus_ = false;
    } // widgets re-focus themselves if clicked
    auto typing = [&](std::string& buf, size_t maxLen, bool focus)
    {
        if (!focus) return;
        for (char c : in.chars) if (c >= 32 && c < 127 && buf.size() < maxLen) buf += c;
        for (int vk : in.keys) if (vk == VK_BACK && !buf.empty()) buf.pop_back();
    };
    typing(friendInput_, 24, friendFocus_);
    typing(profileInput_, 32, profileFocus_);
    if ((friendFocus_ || profileFocus_)) for (int vk : in.keys) if (vk == VK_ESCAPE) friendFocus_ = profileFocus_ =
        false;
    if (rebindActive_)
    {
        for (int vk : in.keys)
        {
            if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LBUTTON || vk == VK_RBUTTON) continue;
            if (vk != VK_ESCAPE)
            {
                (rebindSlot_ == 1 ? s_.vkToggle : rebindSlot_ == 2 ? s_.vkConfig : s_.vkExit) = vk;
                Save();
            }
            rebindActive_ = false;
            break;
        }
    }

    // ---- panel chrome ----
    g.FillRRect(px_ + 2, py_ + 5, kW, panelH, 8.f, Col{0, 0, 0, 0.35f}); // drop shadow
    g.FillRRect(px_, py_, kW, panelH, 8.f, kPanelBg);
    g.FillRRect(px_, py_, kW, kTitleH + 4, 8.f, kTitleBg);
    g.FillRect(px_ + 1, py_ + kTitleH, kW - 2, 4, kPanelBg); // square off the title bar's lower corners
    g.FillRect(px_ + 12, py_ + kTitleH - 3, 62, 2, kAccent);
    g.Text("ESP CONFIG", px_ + 12, py_ + 6, Font::UiBold, kText);
    g.TextRight("drag to move  |  right-click cycles backwards", px_ + kW - 10, py_ + 8, Font::Hint, kDim);

    // ---- tabs ----
    const float tabW = kW / 7.f;
    for (int t = 0; t < 7; ++t)
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
    }
    contentH_[tab_] = y_ - startY + kPad;
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
