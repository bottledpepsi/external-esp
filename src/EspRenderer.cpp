#include "EspRenderer.h"
#include "Avatar.h"
#include "FriendList.h"
#include "ItemText.h"
#include "Presets.h"
#include "ScreenBox.h"
#include "SkinCache.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

using presets::kUnlimited;

namespace
{
    // Same constants as the C# tool.
    constexpr float TRACER_NEAR = 10.f, TRACER_FAR = 60.f;
    constexpr float BOX_NEAR = 10.f, BOX_FAR = 80.f;
    constexpr double kPlayerH = 1.8, kPlayerW = 0.6; // Minecraft player hitbox (standing)
    constexpr double kDeg2Rad = 3.14159265358979323846 / 180.0;

    // The colors come from the settings (Theme.h): player boxes / tracers / dots, friends, name text, distance text, items, the death marker.
    Col C(const Rgba& c) { return Col{c.r, c.g, c.b, c.a}; }
    constexpr float kDistLineH = 13.f; // the distance text under a box (Font::UiSmall, 11 px)

    Font NameFont(const Settings& s)
    {
        return s.nameSizeIdx == 0 ? Font::Name10 : s.nameSizeIdx == 2 ? Font::Name17 : Font::Name13;
    }

    float Clamp01(float v) { return std::clamp(v, 0.f, 1.f); }

    std::string DistText(const Settings& s, float dist)
    {
        char b[24];
        if (s.distFmtIdx == 0) std::snprintf(b, sizeof b, "%dm", static_cast<int>(dist));
        else std::snprintf(b, sizeof b, "%.2fk", dist / 1000.f);
        return b;
    }
} // namespace

void EspRenderer::QueueLabels(const PlayerState& p, float dist, bool isFriend, float cx, float top, float bottom)
{
    if (nReq_ >= static_cast<int>(reqs_.size())) return; // more players than the wire can carry: the extra labels are not drawn
    reqs_[static_cast<size_t>(nReq_++)] = LabelReq{&p, dist, cx, top, bottom, isFriend};
}

// Declutter on: nearest first, each label takes its place or is moved out of the way. Off: far first, near on top, as before.
void EspRenderer::FlushLabels(Gfx& g, const Settings& s)
{
    if (s.labelDeclutter)
        for (int i = nReq_ - 1; i >= 0; --i) DrawLabels(g, s, reqs_[static_cast<size_t>(i)]);
    else
        for (int i = 0; i < nReq_; ++i) DrawLabels(g, s, reqs_[static_cast<size_t>(i)]);
    nReq_ = 0;
}

// Names above, distance below, friend avatar placeholder next to the name.
void EspRenderer::DrawLabels(Gfx& g, const Settings& s, const LabelReq& r)
{
    const PlayerState& p = *r.p;
    const float dist = r.dist, cx = r.cx;
    const float alpha = labels::Fade(dist, s.labelDist, s.labelFadePct);
    if (alpha < 0.03f) return; // beyond the label distance (or all but gone)
    const bool isFriend = r.isFriend;
    if (s.showPlayerNames && !p.name.empty())
    {
        const Font f = NameFont(s);
        const float px = presets::NameSizes[s.nameSizeIdx];
        const float a = px + 2.f; // the avatar square, and the height of a name line
        const float tw = g.TextWidth(p.name, f);
        float y = r.top - px - 7.f;
        bool draw = true;
        if (s.labelDeclutter)
        {
            // the friend avatar sits left of the name: it takes space too
            const float extra = isFriend ? a + 4.f : 0.f;
            labels::Rect box{cx - tw * 0.5f - extra, y, cx + tw * 0.5f, y + a};
            draw = placer_.Place(box, -1, a, 3);
            y = box.y0;
        }
        if (draw)
        {
            g.TextCentered(p.name, cx, y, f, (isFriend ? C(s.colors.friendly) : C(s.colors.text)).with(C(s.colors.text).a * alpha), s.textShadow);
            if (isFriend)
            {
                // avatar placeholder (see Avatar.h)
                const float ax = cx - tw * 0.5f - a - 4.f;
                SkinCache::Request(p.name, p.uuid); // idempotent; fetches the skin face in the background
                if (auto img = SkinCache::Get(p.name))
                {
                    g.DrawImage(*img, ax, y + 1, a, a, alpha);
                    g.Rect(ax - 0.5f, y + 0.5f, a + 1.f, a + 1.f, C(s.colors.friendly).with(0.8f * alpha), 1.f);
                }
                else
                {
                    // loading / offline player: letter placeholder
                    const AvatarColor ac = AvatarColorFor(p.name);
                    g.FillRRect(ax, y + 1, a, a, 2.f, Col{ac.r, ac.g, ac.b, 0.9f * alpha});
                    g.TextCentered(AvatarInitial(p.name), ax + a * 0.5f, y + 1, Font::Hint, Col{1, 1, 1, alpha});
                }
            }
        }
    }
    if (s.distFmtIdx < 2)
    {
        const std::string d = DistText(s, dist);
        float y = r.bottom + 3.f;
        bool draw = true;
        if (s.labelDeclutter)
        {
            const float tw = g.TextWidth(d, Font::UiSmall);
            labels::Rect box{r.cx - tw * 0.5f, y, r.cx + tw * 0.5f, y + kDistLineH};
            draw = placer_.Place(box, +1, kDistLineH, 2);
            y = box.y0;
        }
        if (draw) g.TextCentered(d, r.cx, y, Font::UiSmall, C(s.colors.distance).with(s.colors.distance.a * alpha), s.textShadow);
    }
}

void EspRenderer::DrawTracer(Gfx& g, const Settings& s, const Camera& cam, const Vec3& target, float dist,
                             bool isFriend, const Col* fixed)
{
    if (dist > presets::TracerDist[static_cast<size_t>(std::clamp(s.tracerMaxIdx, 0, static_cast<int>(presets::TracerDist.size()) - 1))]) return;
    double xc, yc, zc;
    cam.ToCamera(target, xc, yc, zc);
    if (zc < Camera::kNear) return; // behind the camera
    // Off-screen aware: clamp NDC to the screen edge so the tracer still points toward the target.
    const float ndcX = std::clamp(static_cast<float>(xc / zc * cam.k / cam.aspect), -1.f, 1.f);
    const float ndcY = std::clamp(static_cast<float>(yc / zc * cam.k), -1.f, 1.f);
    const float tx = (ndcX + 1.f) * 0.5f * cam.w, ty = (1.f - ndcY) * 0.5f * cam.h;
    const float ox = cam.w * 0.5f, oy = s.tracerOriginIdx == 0 ? static_cast<float>(cam.h) : s.tracerOriginIdx == 2 ? 0.f : cam.h * 0.5f;

    float opacity = presets::TraceOpacities[s.tracerOpacityIdx];
    if (s.tracerFade) opacity *= 1.f - Clamp01((dist - TRACER_NEAR) / (TRACER_FAR * 2.f - TRACER_NEAR));
    if (opacity <= 0.01f) return;

    Col c;
    if (fixed) c = fixed->with(opacity);
    else if (s.tracerColorIdx == 1) c = Col{1, 1, 1, opacity};
    else if (isFriend) c = C(s.colors.friendly).with(opacity);
    else
    {
        const float t = Clamp01((dist - TRACER_NEAR) / (TRACER_FAR - TRACER_NEAR));
        c = Col{1.f - t, t, 0.f, opacity};
    } // near = red, far = green
    g.Line(ox, oy, tx, ty, c, presets::Thickness[s.tracerThickIdx]);
}

void EspRenderer::DrawSkeleton(Gfx& g, const Settings& s, const Camera& cam, const PlayerState& p, float dist,
                               bool isFriend)
{
    const double yaw = p.yawValid ? p.yawDeg * kDeg2Rad : 0.0;
    const double sy = (p.sizeValid ? p.height : kPlayerH) / kPlayerH; // sneaking players are shorter
    const double rx = -std::cos(yaw), rz = -std::sin(yaw); // player's right
    const double fx = -std::sin(yaw), fz = std::cos(yaw); // player's forward
    auto J = [&](double side, double fwd, double y)
    {
        return Vec3{p.pos.x + rx * side + fx * fwd, p.pos.y + y * sy, p.pos.z + rz * side + fz * fwd};
    };
    float tx, ty;
    if (!cam.Project(J(0, 0, 0.9), tx, ty)) return; // body centre must be in front of the camera

    struct P2
    {
        float x, y;
    };
    auto pr = [&](const Vec3& v)
    {
        P2 o;
        cam.ProjectClamped(v, o.x, o.y);
        return o;
    };
    const P2 head = pr(J(0, 0, 1.62)), headTop = pr(J(0, 0, 1.87)), neck = pr(J(0, 0, 1.45));
    const P2 shL = pr(J(-0.30, 0, 1.40)), shR = pr(J(0.30, 0, 1.40));
    const P2 hipC = pr(J(0, 0, 0.72)), hipL = pr(J(-0.12, 0, 0.72)), hipR = pr(J(0.12, 0, 0.72));
    const P2 handL = pr(J(-0.37, 0.06, 0.72)), handR = pr(J(0.37, 0.06, 0.72));
    const P2 footL = pr(J(-0.12, 0, 0)), footR = pr(J(0.12, 0, 0));

    Col c = isFriend ? C(s.colors.friendly).with(0.95f) : C(s.colors.enemy).with(0.95f);
    if (!isFriend && s.playerColorIdx == 1)
    {
        const float t = Clamp01((dist - BOX_NEAR) / (BOX_FAR - BOX_NEAR));
        c = Col{1.f - t, t, 0.f, 0.95f};
    }
    const float sw = std::max(1.5f, presets::Thickness[s.boxThickIdx]);
    const float headR = std::max(3.f, std::hypot(head.x - headTop.x, head.y - headTop.y));
    g.Circle(head.x, head.y, headR, c, sw);
    g.Line(neck.x, neck.y, hipC.x, hipC.y, c, sw);
    g.Line(shL.x, shL.y, shR.x, shR.y, c, sw);
    g.Line(shL.x, shL.y, handL.x, handL.y, c, sw);
    g.Line(shR.x, shR.y, handR.x, handR.y, c, sw);
    g.Line(hipL.x, hipL.y, hipR.x, hipR.y, c, sw);
    g.Line(hipL.x, hipL.y, footL.x, footL.y, c, sw);
    g.Line(hipR.x, hipR.y, footR.x, footR.y, c, sw);

    QueueLabels(p, dist, isFriend, head.x, head.y - headR, std::max(footL.y, footR.y));
}

void EspRenderer::DrawPlayer(Gfx& g, const Settings& s, const Camera& cam, const PlayerState& p, float dist,
                             bool isFriend)
{
    if (s.boxStyleIdx == static_cast<int>(BoxStyle::Skeleton))
    {
        DrawSkeleton(g, s, cam, p, dist, isFriend);
        return;
    }

    const double ph = p.sizeValid ? p.height : kPlayerH, pw = p.sizeValid ? p.width : kPlayerW;
    // the game's hitbox when known; boxes smaller than the minimum are grown around their centre, never dropped (ScreenBox.h). A player is
    // 0.6 wide and 1.8 tall, so the width reaches 2 px at a third of the distance where the height does: the old "< 2 px" test made far players
    // vanish while their height was still 6 px (tests/player_cull_test.cpp). Far players are limited by MaxPlayerDist, not by their size.
    ScreenBox sb;
    if (!ProjectBox(cam, p.pos, pw, ph, s.playerMinPx, sb)) return; // behind the camera, or entirely off-screen
    const float minX = sb.x0, minY = sb.y0, maxX = sb.x1, maxY = sb.y1;
    const float bx = minX, by = minY, bw = maxX - minX, bh = maxY - minY;

    const Col base = isFriend ? C(s.colors.friendly) : C(s.colors.enemy);
    const float fill = presets::Opacities[s.boxFillIdx];
    if (fill > 0.f) g.FillRect(bx, by, bw, bh, base.with(fill));

    Col outline = base.with(0.90f);
    if (!isFriend && s.playerColorIdx == 1)
    {
        // Gradient: red (close) -> green (far)
        const float t = Clamp01((dist - BOX_NEAR) / (BOX_FAR - BOX_NEAR));
        outline = Col{1.f - t, t, 0.f, 0.90f};
    }
    const float thick = presets::Thickness[s.boxThickIdx], cr = presets::CornerRadius[s.cornerRadiusIdx];
    switch (static_cast<BoxStyle>(s.boxStyleIdx))
    {
    case BoxStyle::CornerOnly:
        {
            const float t = std::min(bw * 0.3f, 10.f);
            g.Line(bx, by, bx + t, by, outline, thick);
            g.Line(bx, by, bx, by + t, outline, thick);
            g.Line(bx + bw, by, bx + bw - t, by, outline, thick);
            g.Line(bx + bw, by, bx + bw, by + t, outline, thick);
            g.Line(bx, by + bh, bx + t, by + bh, outline, thick);
            g.Line(bx, by + bh, bx, by + bh - t, outline, thick);
            g.Line(bx + bw, by + bh, bx + bw - t, by + bh, outline, thick);
            g.Line(bx + bw, by + bh, bx + bw, by + bh - t, outline, thick);
            break;
        }
    case BoxStyle::Rounded: g.RRect(bx, by, bw, bh, cr, outline, thick);
        break;
    default: g.Rect(bx, by, bw, bh, outline, thick);
        break;
    }

    if (s.showHealth && p.healthValid)
    {
        // vertical bar on the box's left edge
        const float ratio = Clamp01(p.health / 20.f);
        const float barX = bx - 5.f - thick * 0.5f;
        g.FillRect(barX, by, 3.f, bh, Col{0, 0, 0, 0.55f});
        const float fh = bh * ratio;
        g.FillRect(barX, by + bh - fh, 3.f, fh, Col{1.f - ratio, ratio, 0.10f, 1.f});
    }
    QueueLabels(p, dist, isFriend, bx + bw * 0.5f, by, by + bh);
}

void EspRenderer::DrawItem(Gfx& g, const Settings& s, const Camera& cam, const ItemState& it, float dist)
{
    const double iw = it.sizeValid ? it.width : 0.25, ih = it.sizeValid ? it.height : 0.25;
    ScreenBox sb;
    if (!ProjectBox(cam, it.pos, iw, ih, s.itemMinPx, sb)) return; // small distant items are kept at a minimum marker (default 6 px)
    const float minX = sb.x0, minY = sb.y0, maxX = sb.x1, maxY = sb.y1;
    const float op = presets::TraceOpacities[s.itemOpacityIdx];
    const bool enchanted = !it.enchants.empty();
    const Col boxCol = (enchanted ? C(s.colors.itemEnchanted) : C(s.colors.item)).with(op * (enchanted ? s.colors.itemEnchanted.a : s.colors.item.a));
    g.Rect(minX, minY, maxX - minX, maxY - minY, boxCol, 1.f);
    const float alpha = labels::Fade(dist, s.labelDist, s.labelFadePct);
    if (alpha < 0.03f) return; // beyond the label distance (or all but gone)

    // Label, reading top to bottom: name xN / enchantments / tags. Drawn bottom-up so the last line sits on the box.
    struct Line
    {
        std::string text;
        Col col;
        Font font;
    };
    std::vector<Line> lines;
    if (s.showItemNames)
    {
        std::string label = PrettyItemName(it.key);
        if (s.showItemCount && it.count > 1) label += " x" + std::to_string(it.count);
        lines.push_back({label, boxCol, Font::Name13});
    }
    if (s.showItemEnchants)
    {
        size_t shown = 0;
        for (const Enchant& e : it.enchants)
        {
            if (shown++ >= 6)
            {
                lines.push_back({
                    "+" + std::to_string(it.enchants.size() - 6) + " more", Col{0.7f, 0.7f, 0.85f, op}, Font::UiSmall
                });
                break;
            }
            lines.push_back({
                PrettyEnchant(e), IsCurse(e) ? Col{1.f, 0.45f, 0.45f, op} : Col{0.80f, 0.62f, 1.f, op}, Font::UiSmall
            });
        }
    }
    if (s.showItemTags && !it.tags.empty())
    {
        // raw NBT / component text, wrapped, at most 3 lines
        for (size_t pos = 0, n = 0; pos < it.tags.size() && n < 3; pos += 58, ++n)
            lines.push_back({
                it.tags.substr(pos, 58) + (n == 2 && pos + 58 < it.tags.size() ? "..." : ""),
                Col{0.62f, 0.66f, 0.75f, op * 0.9f}, Font::Hint
            });
    }
    if (lines.empty()) return;
    const float cx = (minX + maxX) * 0.5f;
    auto lineH = [](Font f) { return f == Font::Name13 ? 17.f : 14.f; };
    float bottomY = minY - 4.f; // the block's lower edge: the last line sits on the box
    if (s.labelDeclutter)
    {
        // the whole block is one rectangle; it moves up past other labels (players' labels are already placed)
        float w = 0.f, h = 0.f;
        for (const Line& ln : lines)
        {
            w = std::max(w, g.TextWidth(ln.text, ln.font));
            h += lineH(ln.font);
        }
        labels::Rect box{cx - w * 0.5f, bottomY - h, cx + w * 0.5f, bottomY};
        if (!placer_.Place(box, -1, 14.f, 4)) return;
        bottomY = box.y1;
    }
    float y = bottomY;
    for (auto ln = lines.rbegin(); ln != lines.rend(); ++ln)
    {
        y -= lineH(ln->font);
        g.TextCentered(ln->text, cx, y, ln->font, ln->col.with(ln->col.a * alpha), s.textShadow);
    }
}

void EspRenderer::DrawEntities(Gfx& g, const Settings& s, const Camera& cam, const std::vector<EntityState>& list)
{
    entShown_ = 0;
    entSeen_ = static_cast<int>(list.size());
    for (int& n : entKindShown_) n = 0;
    if (!s.showEntities) return;
    const float maxDist = presets::EntityDist[static_cast<size_t>(std::clamp(s.entDistIdx, 0, static_cast<int>(presets::EntityDist.size()) - 1))];
    int nReq = 0;
    // boxes far -> near (the agent sends them nearest first), labels afterwards nearest first
    for (int i = static_cast<int>(list.size()) - 1; i >= 0; --i)
    {
        const EntityState& e = list[static_cast<size_t>(i)];
        const int k = static_cast<int>(e.kind);
        if (k < 0 || k >= kEntityKindCount || !s.entShow[k]) continue;
        const float dist = static_cast<float>(cam.Distance(e.pos));
        if (dist > maxDist) continue;
        const bool small = e.kind == EntityKind::Projectile || e.kind == EntityKind::Xp;
        const double w = e.sizeValid ? e.width : small ? 0.4 : kPlayerW, h = e.sizeValid ? e.height : small ? 0.4 : kPlayerH;
        ScreenBox sb;
        if (!ProjectBox(cam, e.pos, w, h, small ? s.itemMinPx : s.playerMinPx, sb)) continue;
        const presets::Rgb rgb = presets::EntityPaletteRgb[static_cast<size_t>(std::clamp(s.entColor[k], 0, static_cast<int>(presets::EntityPaletteRgb.size()) - 1))];
        const Col col{rgb.r, rgb.g, rgb.b, small ? 0.80f : 0.90f};
        const float bx = sb.x0, by = sb.y0, bw = sb.x1 - sb.x0, bh = sb.y1 - sb.y0;
        const float thick = small ? 1.f : presets::Thickness[s.boxThickIdx];
        g.Rect(bx, by, bw, bh, col, thick);
        if (s.showTracers && s.tracerEntity[k])
        {
            const double ty = s.tracerTargetIdx == 0 ? e.pos.y : s.tracerTargetIdx == 2 ? e.pos.y + h : e.pos.y + h * 0.5;
            DrawTracer(g, s, cam, {e.pos.x, ty, e.pos.z}, dist, false, &col);
        }
        if (s.showHealth && e.healthValid && !small)
        {
            const float ratio = Clamp01(e.health / 20.f);
            const float barX = bx - 5.f - thick * 0.5f;
            g.FillRect(barX, by, 3.f, bh, Col{0, 0, 0, 0.55f});
            const float fh = bh * ratio;
            g.FillRect(barX, by + bh - fh, 3.f, fh, Col{1.f - ratio, ratio, 0.10f, 1.f});
        }
        ++entShown_;
        ++entKindShown_[k];
        if (s.entLabels && !small && nReq < static_cast<int>(entReq_.size())) entReq_[static_cast<size_t>(nReq++)] = EntReq{&e, dist, bx + bw * 0.5f, by, by + bh};
    }
    for (int i = nReq - 1; i >= 0; --i) // nearest first: the nearest mob keeps its place
    {
        const EntReq& r = entReq_[static_cast<size_t>(i)];
        const float alpha = labels::Fade(r.dist, s.labelDist, s.labelFadePct);
        if (alpha < 0.03f) continue;
        const int k = static_cast<int>(r.e->kind);
        const presets::Rgb rgb = presets::EntityPaletteRgb[static_cast<size_t>(std::clamp(s.entColor[k], 0, static_cast<int>(presets::EntityPaletteRgb.size()) - 1))];
        const Font f = NameFont(s);
        const float px = presets::NameSizes[s.nameSizeIdx], a = px + 2.f;
        const std::string& name = r.e->label.empty() ? std::string("Mob") : r.e->label;
        float y = r.top - px - 7.f;
        bool draw = true;
        if (s.labelDeclutter)
        {
            const float tw = g.TextWidth(name, f);
            labels::Rect box{r.cx - tw * 0.5f, y, r.cx + tw * 0.5f, y + a};
            draw = placer_.Place(box, -1, a, 3);
            y = box.y0;
        }
        if (draw) g.TextCentered(name, r.cx, y, f, Col{rgb.r, rgb.g, rgb.b, alpha}, s.textShadow);
        if (s.distFmtIdx < 2)
        {
            const std::string d = DistText(s, r.dist);
            float dy = r.bottom + 3.f;
            bool drawD = true;
            if (s.labelDeclutter)
            {
                const float tw = g.TextWidth(d, Font::UiSmall);
                labels::Rect box{r.cx - tw * 0.5f, dy, r.cx + tw * 0.5f, dy + kDistLineH};
                drawD = placer_.Place(box, +1, kDistLineH, 2);
                dy = box.y0;
            }
            if (drawD) g.TextCentered(d, r.cx, dy, Font::UiSmall, C(s.colors.distance).with(s.colors.distance.a * alpha), s.textShadow);
        }
    }
}

const EntityState* EspRenderer::PickNearestCrosshair(const Settings& s, const Camera& cam, const std::vector<EntityState>& list, float maxPx) const
{
    const float maxDist = presets::EntityDist[static_cast<size_t>(std::clamp(s.entDistIdx, 0, static_cast<int>(presets::EntityDist.size()) - 1))];
    const EntityState* best = nullptr;
    float bestPx = maxPx;
    for (const EntityState& e : list)
    {
        const int k = static_cast<int>(e.kind);
        if (k < 0 || k >= kEntityKindCount || !s.entShow[k] || cam.Distance(e.pos) > maxDist) continue;
        const bool small = e.kind == EntityKind::Projectile || e.kind == EntityKind::Xp;
        const double h = e.sizeValid ? e.height : small ? 0.4 : kPlayerH;
        float sx, sy;
        if (!cam.Project({e.pos.x, e.pos.y + h * 0.5, e.pos.z}, sx, sy)) continue; // behind the camera
        const float d = std::hypot(sx - cam.w * 0.5f, sy - cam.h * 0.5f);
        if (d < bestPx)
        {
            bestPx = d;
            best = &e;
        }
    }
    return best;
}

void EspRenderer::DrawInspector(Gfx& g, const Settings& s, const Camera& cam, int winW, int winH, const InspectView& v)
{
    if (v.entity)
    {
        const EntityState& e = *v.entity;
        const bool small = e.kind == EntityKind::Projectile || e.kind == EntityKind::Xp;
        ScreenBox sb;
        const double w = e.sizeValid ? e.width : small ? 0.4 : kPlayerW, h = e.sizeValid ? e.height : small ? 0.4 : kPlayerH;
        if (ProjectBox(cam, e.pos, w, h, 6.f, sb)) g.Rect(sb.x0 - 3.f, sb.y0 - 3.f, sb.x1 - sb.x0 + 6.f, sb.y1 - sb.y0 + 6.f, Col{1.f, 1.f, 1.f, 0.95f}, 2.f);
    }
    (void)s;
    constexpr float kW = 420.f, kRowH = 15.f;
    const size_t nRows = v.rows ? std::min<size_t>(v.rows->size(), 34) : 0;
    const float x = static_cast<float>(winW) - kW - 12.f;
    const float h = 26.f + (nRows ? static_cast<float>(nRows) * kRowH : kRowH) + 10.f;
    const float y = std::clamp(static_cast<float>(presets::MinimapSizes[std::clamp(s.minimapSizeIdx, 0, static_cast<int>(presets::MinimapSizes.size()) - 1)]) + 40.f, 40.f,
                               std::max(40.f, static_cast<float>(winH) - h - 10.f));
    g.FillRRect(x, y, kW, h, 6.f, Col{0.04f, 0.05f, 0.08f, 0.82f});
    g.RRect(x, y, kW, h, 6.f, Col{0.45f, 0.55f, 0.85f, 0.8f}, 1.f);
    g.Text(v.title, x + 8.f, y + 5.f, Font::Ui, Col{0.95f, 0.97f, 1.f, 1.f}, true);
    float ry = y + 26.f;
    if (!v.rows)
    {
        g.Text(v.note, x + 8.f, ry, Font::UiSmall, Col{0.95f, 0.8f, 0.4f, 1.f}, true);
        return;
    }
    auto fit = [](const std::string& str, size_t n) { return str.size() <= n ? str : str.substr(0, n - 1) + "~"; };
    for (size_t i = 0; i < nRows; ++i, ry += kRowH)
    {
        const InspectRow& r = (*v.rows)[i];
        g.Text(fit(r.key, 30), x + 8.f, ry, Font::UiSmall, Col{0.62f, 0.67f, 0.80f, 1.f}, true);
        g.Text(fit(r.value, 38), x + 176.f, ry, Font::UiSmall, Col{0.90f, 0.93f, 0.98f, 1.f}, true);
    }
}

void EspRenderer::DrawGhosts(Gfx& g, const Settings& s, const Camera& cam, const WorldSnapshot& raw)
{
    const Col ghost{1.f, 0.2f, 0.9f, 0.85f};
    const float maxDist = presets::PlayerDist[s.maxPlayerDistIdx];
    if (s.showPlayers)
        for (const PlayerState& p : raw.players)
        {
            if (static_cast<float>(cam.Distance(p.pos)) > maxDist) continue;
            ScreenBox sb;
            const double ph = p.sizeValid ? p.height : kPlayerH, pw = p.sizeValid ? p.width : kPlayerW;
            if (ProjectBox(cam, p.pos, pw, ph, s.playerMinPx, sb)) g.Rect(sb.x0, sb.y0, sb.x1 - sb.x0, sb.y1 - sb.y0, ghost, 1.f);
        }
    if (s.showItems)
        for (const ItemState& it : raw.items)
        {
            ScreenBox sb;
            const double iw = it.sizeValid ? it.width : 0.25, ih = it.sizeValid ? it.height : 0.25;
            if (ProjectBox(cam, it.pos, iw, ih, s.itemMinPx, sb)) g.Rect(sb.x0, sb.y0, sb.x1 - sb.x0, sb.y1 - sb.y0, ghost.with(0.6f), 1.f);
        }
}

bool EspRenderer::ProbeNearestPlayer(const Settings& s, const Camera& cam, const WorldSnapshot& snap, double& x, double& y) const
{
    // the nearest player whose box is really on the screen (in front of the camera, inside the viewport plus 10 %)
    double bestD = 1e30;
    bool found = false;
    const double maxDist = presets::PlayerDist[s.maxPlayerDistIdx];
    for (const PlayerState& p : snap.players)
    {
        const double d = cam.Distance(p.pos);
        if (d < 1.5 || d > maxDist || d >= bestD) continue;
        float cx, cy;
        const double ph = p.sizeValid ? p.height : kPlayerH;
        if (!cam.Project({p.pos.x, p.pos.y + ph * 0.5, p.pos.z}, cx, cy)) continue; // behind the camera: no position at all
        ScreenBox sb;
        const double pw = p.sizeValid ? p.width : kPlayerW;
        if (!ProjectBox(cam, p.pos, pw, ph, s.playerMinPx, sb)) continue;
        const double bx = (sb.x0 + sb.x1) * 0.5, by = (sb.y0 + sb.y1) * 0.5;
        if (!(bx > -0.1 * cam.w && bx < 1.1 * cam.w && by > -0.1 * cam.h && by < 1.1 * cam.h)) continue;
        bestD = d;
        x = bx;
        y = by;
        found = true;
    }
    return found;
}

void EspRenderer::DrawGraph(Gfx& g, float x, float y, float w, float h, const perf::Ring<perf::PerfHistory::kN>& r, const std::string& caption, const char* unit)
{
    const float top = perf::NiceCeiling(r.Summarize().max * 1.1f); // ms at the top edge of the graph
    g.FillRRect(x, y, w, h, 4.f, Col{0.04f, 0.05f, 0.08f, 0.72f});
    g.Line(x, y + h * 0.5f, x + w, y + h * 0.5f, Col{1, 1, 1, 0.10f}, 1.f); // half way
    char t[24];
    std::snprintf(t, sizeof t, "%.0f %s", top, unit); // a handful of distinct strings (4, 8, 16, 32 ...): the text cache keeps hitting
    g.Text(t, x + 4, y + 1, Font::Hint, Col{0.7f, 0.75f, 0.85f, 0.7f});
    const int n = r.Size();
    if (n >= 2)
    {
        const float dx = w / static_cast<float>(perf::PerfHistory::kN - 1);
        const float x0 = x + w - dx * static_cast<float>(n - 1); // the newest value sits at the right edge
        float px = x0, py = y + h - std::min(r.At(0) / top, 1.f) * (h - 2.f) - 1.f;
        for (int i = 1; i < n; ++i)
        {
            const float cx = x0 + dx * static_cast<float>(i), cy = y + h - std::min(r.At(i) / top, 1.f) * (h - 2.f) - 1.f;
            g.Line(px, py, cx, cy, Col{0.35f, 0.85f, 1.f, 0.95f}, 1.f);
            px = cx;
            py = cy;
        }
    }
    g.Rect(x, y, w, h, Col{1, 1, 1, 0.18f}, 1.f);
    if (!caption.empty()) g.Text(caption, x, y + h + 2.f, Font::UiSmall, Col{0.62f, 0.67f, 0.80f, 0.95f}, true);
}

void EspRenderer::DrawReplayBar(Gfx& g, int winW, int winH, const ReplayBar& bar)
{
    const float w = std::min(640.f, winW * 0.5f), h = 8.f, x = (winW - w) * 0.5f, y = static_cast<float>(winH) - 46.f;
    const float frac = bar.totalSec > 0 ? Clamp01(static_cast<float>(bar.elapsedSec / bar.totalSec)) : 0.f;
    g.FillRRect(x, y, w, h, 3.f, Col{0.05f, 0.06f, 0.09f, 0.75f});
    if (frac > 0.f) g.FillRRect(x, y, w * frac, h, 3.f, bar.paused ? Col{1.f, 0.75f, 0.25f, 0.95f} : Col{0.35f, 0.75f, 1.f, 0.95f});
    g.RRect(x, y, w, h, 3.f, Col{1, 1, 1, 0.25f}, 1.f);
    for (int i = 0; i < bar.nMarks; ++i)
    {
        const float mx = x + w * Clamp01(bar.marks[i]);
        g.Line(mx, y - 4.f, mx, y + h + 4.f, Col{1.f, 0.85f, 0.25f, 1.f}, 2.f);
    }
    auto clock = [](double s)
    {
        const int t = static_cast<int>(std::max(0.0, s));
        char b[24];
        std::snprintf(b, sizeof b, "%d:%02d", t / 60, t % 60);
        return std::string(b);
    };
    char sp[16];
    std::snprintf(sp, sizeof sp, "%.2gx", bar.speed);
    g.TextCentered(clock(bar.elapsedSec) + " / " + clock(bar.totalSec) + "   " + sp + (bar.paused ? "   paused" : ""), x + w * 0.5f, y + h + 6.f, Font::UiSmall,
                   Col{0.85f, 0.88f, 0.95f, 0.95f}, true);
}

void EspRenderer::DrawPerfPanel(Gfx& g, int winH, const perf::PerfHistory& hst, const Settings* s)
{
    constexpr float kW = 260.f, kH = 56.f, kGap = 22.f; // a graph, and the caption under it
    constexpr float kBarH = 10.f, kHistH = 40.f;
    const bool extras = winH >= 800; // the wire graph, the latency bar and the histogram need room; a small window keeps the two basic graphs
    const bool hist = extras && s && s->showEntities;
    const int nGraphs = extras ? 3 : 2;
    const float x = 10.f;
    float y = static_cast<float>(winH) - 10.f - 16.f /* jitter line */ - static_cast<float>(nGraphs) * (kH + kGap) - (extras ? kBarH + 22.f : 0.f) - (hist ? kHistH + 22.f : 0.f);
    DrawGraph(g, x, y, kW, kH, hst.frameMs, hst.frameText);
    y += kH + kGap;
    DrawGraph(g, x, y, kW, kH, hst.ageMs, hst.ageText);
    y += kH + kGap;
    if (extras)
    {
        DrawGraph(g, x, y, kW, kH, hst.wireKBps, hst.wireText, "KB/s");
        y += kH + kGap;
        // the latency of what is on the screen in pieces: the age of the newest sample, the wait between frames, the overlay's own time
        const perf::PerfHistory::Pipeline p = hst.Breakdown();
        g.FillRRect(x, y, kW, kBarH, 3.f, Col{0.04f, 0.05f, 0.08f, 0.72f});
        if (p.valid && p.total > 0.0)
        {
            const double seg[3] = {p.ageMs, p.frameMs, p.cpuMs};
            const Col col[3] = {Col{0.35f, 0.85f, 1.f, 0.95f}, Col{0.45f, 0.55f, 1.f, 0.95f}, Col{0.40f, 0.90f, 0.45f, 0.95f}};
            float sx = x;
            for (int i = 0; i < 3; ++i)
            {
                const float sw = static_cast<float>(seg[i] / p.total) * kW;
                if (sw > 0.5f) g.FillRect(sx, y, sw, kBarH, col[i]);
                sx += sw;
            }
        }
        g.Rect(x, y, kW, kBarH, Col{1, 1, 1, 0.18f}, 1.f);
        if (!hst.pipeText.empty()) g.Text(hst.pipeText, x, y + kBarH + 2.f, Font::UiSmall, Col{0.62f, 0.67f, 0.80f, 0.95f}, true);
        y += kBarH + 22.f;
    }
    if (hist)
    {
        // entities by category, as bars: hostile, passive, neutral, projectile, xp, other
        static const EntityKind order[] = {EntityKind::Hostile, EntityKind::Passive, EntityKind::Neutral, EntityKind::Projectile, EntityKind::Xp, EntityKind::Other};
        static const char* const names[] = {"hostile", "passive", "neutral", "proj.", "xp", "other"};
        int mx = 1;
        for (int n : hst.entKind) mx = std::max(mx, n);
        const float slot = kW / 6.f;
        for (int i = 0; i < 6; ++i)
        {
            const int k = static_cast<int>(order[i]);
            const int n = hst.entKind[k];
            const presets::Rgb c = presets::EntityPaletteRgb[static_cast<size_t>(std::clamp(s->entColor[k], 0, static_cast<int>(presets::EntityPaletteRgb.size()) - 1))];
            const float bh = static_cast<float>(n) / static_cast<float>(mx) * (kHistH - 12.f);
            const float bx = x + slot * static_cast<float>(i) + 4.f;
            if (n > 0) g.FillRect(bx, y + kHistH - bh, slot - 8.f, bh, Col{c.r, c.g, c.b, s->entShow[k] ? 0.9f : 0.35f});
            g.TextCentered(std::to_string(n), bx + (slot - 8.f) * 0.5f, y + kHistH - bh - 12.f, Font::Hint, Col{0.80f, 0.84f, 0.92f, 0.95f});
            g.TextCentered(names[i], bx + (slot - 8.f) * 0.5f, y + kHistH + 1.f, Font::Hint, Col{0.62f, 0.67f, 0.80f, 0.95f});
        }
        g.Line(x, y + kHistH, x + kW, y + kHistH, Col{1, 1, 1, 0.18f}, 1.f);
        y += kHistH + 22.f;
    }
    if (!hst.jitterText.empty()) g.Text(hst.jitterText, x, y - 4.f, Font::UiSmall, Col{0.62f, 0.67f, 0.80f, 0.95f}, true);
}

void EspRenderer::DrawWorld(Gfx& g, const Settings& s, const Camera& cam, const WorldSnapshot& snap)
{
    const float maxDist = presets::PlayerDist[s.maxPlayerDistIdx];
    struct E
    {
        const PlayerState* p;
        float dist;
        bool fr;
    };
    std::vector<E> list;
    placer_.Reset();
    nReq_ = 0;
    itemsShown_ = itemsFiltered_ = 0;
    for (const auto& p : snap.players)
    {
        const float d = static_cast<float>(cam.Distance(p.pos));
        if (d > maxDist) continue;
        list.push_back({&p, d, FriendList::Contains(p.name)});
    }
    std::sort(list.begin(), list.end(), [](const E& a, const E& b) { return a.dist > b.dist; });
    // far first -> near on top

    if (s.showTracers && s.tracerPlayers)
        for (const auto& e : list)
        {
            const double eh = e.p->sizeValid ? e.p->height : kPlayerH;
            const double ty = s.tracerTargetIdx == 0
                                  ? e.p->pos.y
                                  : s.tracerTargetIdx == 2
                                  ? e.p->pos.y + eh
                                  : e.p->pos.y + eh * 0.5;
            DrawTracer(g, s, cam, {e.p->pos.x, ty, e.p->pos.z}, e.dist, e.fr);
        }
    if (s.showPlayers)
        for (const auto& e : list) DrawPlayer(g, s, cam, *e.p, e.dist, e.fr);
    FlushLabels(g, s); // names and distances, once all the boxes are down

    if (s.showItems)
    {
        if (itemRulesSrc_ != s.itemFilterList)
        {
            itemRules_.Set(s.itemFilterList);
            itemRulesSrc_ = s.itemFilterList;
        }
        const auto mode = static_cast<itemfilter::Mode>(s.itemFilterMode);
        for (const auto& it : snap.items)
        {
            if (s.hideJunk && presets::IsJunkItem(PrettyItemName(it.key))) // junk list uses display names ("Rotten Flesh")
            {
                ++itemsFiltered_;
                continue;
            }
            if (!itemfilter::Show(mode, itemRules_, s.itemMinTier, it))
            {
                ++itemsFiltered_;
                continue;
            }
            ++itemsShown_;
            DrawItem(g, s, cam, it, static_cast<float>(cam.Distance(it.pos)));
        }
    }
}

// Top-right radar, north-up like the original (+X east = right, +Z south = down).
void EspRenderer::DrawMinimap(Gfx& g, const Settings& s, const Camera& cam, const WorldSnapshot& snap, int winW, int winH, const MinimapExtras& extra)
{
    const int mm = presets::MinimapSizes[s.minimapSizeIdx];
    const float range = static_cast<float>(presets::MinimapRanges[s.minimapRangeIdx]);
    const float pad = 12.f;
    const bool right = s.minimapCorner == 0 || s.minimapCorner == 2, bottom = s.minimapCorner == 2 || s.minimapCorner == 3;
    const float x = right ? winW - mm - pad : pad, y = bottom ? winH - mm - pad : pad, r = mm * 0.5f, cx = x + r, cy = y + r;
    const Col bg{0.05f, 0.06f, 0.09f, 0.72f}, line{0.40f, 0.45f, 0.60f, 0.45f}, border{0.30f, 0.34f, 0.48f, 0.9f};

    g.FillRRect(x, y, static_cast<float>(mm), static_cast<float>(mm), 6.f, bg);
    g.RRect(x, y, static_cast<float>(mm), static_cast<float>(mm), 6.f, border, 1.5f);
    g.Circle(cx, cy, r * 0.5f, line, 0.5f);
    g.Circle(cx, cy, r - 2.f, line, 0.5f);
    g.Line(cx, y + 2, cx, y + mm - 2, line, 0.5f);
    g.Line(x + 2, cy, x + mm - 2, cy, line, 0.5f);

    const Vec3 center = snap.localValid ? snap.local.pos : cam.eye; // in third person the camera is not at the player
    const float scale = (r - 4.f) / range;
    const double yaw = cam.yawDeg * kDeg2Rad;
    const float fx = static_cast<float>(-std::sin(yaw)), fz = static_cast<float>(std::cos(yaw)); // world forward in (x, z)
    const bool rotate = s.minimapMode == 1;
    // A world offset (dx, dz) in blocks -> screen offset in pixels. North-up: 1:1 (+x right, +z down). Rotate: your forward is up, your right is right.
    auto toScreen = [&](double dx, double dz, float& sx, float& sy)
    {
        if (rotate)
        {
            sx = static_cast<float>(-dx * fz + dz * fx) * scale;
            sy = static_cast<float>(-(dx * fx + dz * fz)) * scale;
        }
        else
        {
            sx = static_cast<float>(dx) * scale;
            sy = static_cast<float>(dz) * scale;
        }
    };
    auto clampRim = [&](float& sx, float& sy)
    {
        const float d = std::hypot(sx, sy);
        if (d > r - 4.f)
        {
            const float k = (r - 4.f) / d;
            sx *= k;
            sy *= k;
        }
    };

    // entities first, small, so players are on top of them
    if (extra.entities && s.showEntities)
    {
        const float entMax = presets::EntityDist[static_cast<size_t>(std::clamp(s.entDistIdx, 0, static_cast<int>(presets::EntityDist.size()) - 1))];
        for (const EntityState& e : *extra.entities)
        {
            const int k = static_cast<int>(e.kind);
            if (k < 0 || k >= kEntityKindCount || !s.entShow[k] || cam.Distance(e.pos) > entMax) continue;
            float sx, sy;
            toScreen(e.pos.x - center.x, e.pos.z - center.z, sx, sy);
            if (std::hypot(sx, sy) > r - 4.f) continue; // far entities are not pinned to the rim: it would only be a ring of noise
            const presets::Rgb c = presets::EntityPaletteRgb[static_cast<size_t>(std::clamp(s.entColor[k], 0, static_cast<int>(presets::EntityPaletteRgb.size()) - 1))];
            g.FillCircle(cx + sx, cy + sy, 2.f, Col{c.r, c.g, c.b, 0.85f});
        }
    }
    const float maxDist = presets::PlayerDist[s.maxPlayerDistIdx];
    for (const auto& p : snap.players)
    {
        if (cam.Distance(p.pos) > maxDist) continue;
        float sx, sy;
        toScreen(p.pos.x - center.x, p.pos.z - center.z, sx, sy);
        clampRim(sx, sy); // clamp to the rim
        g.FillCircle(cx + sx, cy + sy, 3.f, FriendList::Contains(p.name) ? C(s.colors.friendly) : C(s.colors.enemy));
    }
    if (extra.hasDeath)
    {
        float sx, sy;
        toScreen(extra.death.x - center.x, extra.death.z - center.z, sx, sy);
        const bool pinned = std::hypot(sx, sy) > r - 4.f;
        clampRim(sx, sy);
        const Col red = C(s.colors.death).with(s.colors.death.a * (pinned ? 0.7f : 1.f));
        g.Line(cx + sx - 4, cy + sy - 4, cx + sx + 4, cy + sy + 4, red, 2.f);
        g.Line(cx + sx - 4, cy + sy + 4, cx + sx + 4, cy + sy - 4, red, 2.f);
    }
    // Self: dot + facing wedge. North-up: the wedge points where you look. Rotate: it always points up.
    const float wx = rotate ? 0.f : fx, wz = rotate ? -1.f : fz;
    g.FillTriangle(cx + wx * 9.f, cy + wz * 9.f, cx - wz * 4.f - wx * 2.f, cy + wx * 4.f - wz * 2.f,
                   cx + wz * 4.f - wx * 2.f, cy - wx * 4.f - wz * 2.f, Col{0.35f, 0.60f, 1.f, 0.95f});
    g.FillCircle(cx, cy, 2.5f, Col{0.35f, 0.60f, 1.f, 1.f});
    g.Text(std::to_string(static_cast<int>(range)) + "m", x + 4, y + mm - 15.f, Font::Hint,
           Col{0.7f, 0.75f, 0.9f, 0.8f});
    if (rotate)
    {
        // the map turns, so say where north went (world -z)
        float nx, ny;
        toScreen(0.0, -1.0, nx, ny);
        const float nl = std::hypot(nx, ny);
        if (nl > 1e-6f) g.TextCentered("N", cx + nx / nl * (r - 9.f), cy + ny / nl * (r - 9.f) - 6.f, Font::Hint, Col{0.95f, 0.55f, 0.55f, 0.95f});
    }
}

void EspRenderer::DrawDeathMarker(Gfx& g, const Settings& s, const Camera& cam, const Vec3& where, int winW, int winH)
{
    const double dist = cam.Distance(where);
    float sx = 0, sy = 0;
    const Vec3 above{where.x, where.y + 1.0, where.z};
    const bool front = cam.Project(above, sx, sy);
    const Col red = C(s.colors.death).with(s.colors.death.a * 0.95f);
    if (!front || sx < 12 || sx > winW - 12 || sy < 12 || sy > winH - 12)
    {
        // off screen or behind: a small arrow-less tag at the nearest edge, so it is known to be somewhere there
        if (!front) { sx = winW * 0.5f; sy = winH - 40.f; } // behind: bottom centre
        sx = std::clamp(sx, 30.f, static_cast<float>(winW) - 30.f);
        sy = std::clamp(sy, 30.f, static_cast<float>(winH) - 30.f);
    }
    const float d = 7.f;
    g.Line(sx, sy - d, sx + d, sy, red, 2.f);
    g.Line(sx + d, sy, sx, sy + d, red, 2.f);
    g.Line(sx, sy + d, sx - d, sy, red, 2.f);
    g.Line(sx - d, sy, sx, sy - d, red, 2.f);
    g.TextCentered("Death  " + std::to_string(static_cast<int>(dist + 0.5)) + "m", sx, sy + d + 3.f, Font::UiSmall, red, true);
}
