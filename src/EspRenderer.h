// EspRenderer.h - draws the ESP (boxes / skeletons / tracers / labels / health / items) and the minimap.
// Port of the drawing half of EspOverlay.cs; the projection comes from Camera.h instead of GL matrices.
#pragma once
#include "Camera.h"
#include "EspConfig.h"
#include "Gfx.h"
#include "ItemFilter.h"
#include "LabelLayout.h"
#include "PerfStats.h"
#include "Types.h"
#include <array>

class EspRenderer
{
public:
    void DrawWorld(Gfx& g, const Settings& s, const Camera& cam, const WorldSnapshot& snap);
    // What the radar shows besides the players: the entities (when shown) and where the local player died (when there is a marker).
    struct MinimapExtras
    {
        const std::vector<EntityState>* entities = nullptr;
        bool hasDeath = false;
        Vec3 death;
    };
    void DrawMinimap(Gfx& g, const Settings& s, const Camera& cam, const WorldSnapshot& snap, int winW, int winH, const MinimapExtras& extra);
    void DrawMinimap(Gfx& g, const Settings& s, const Camera& cam, const WorldSnapshot& snap, int winW, int winH) { DrawMinimap(g, s, cam, snap, winW, winH, MinimapExtras()); }
    // The death marker in the world: a red diamond over the spot with "Death  87m" (clamped to the screen edge when it is off to the side).
    void DrawDeathMarker(Gfx& g, const Settings& s, const Camera& cam, const Vec3& where, int winW, int winH);
    // The "Both" view: the agent's samples exactly as they arrived, as thin magenta outlines (players) and small squares (items), drawn under
    // the normal picture. No labels, tracers or health. `cam` belongs to the same raw snapshot (CameraFromSnapshot).
    void DrawGhosts(Gfx& g, const Settings& s, const Camera& cam, const WorldSnapshot& raw);
    // Mobs, projectiles and experience orbs (wire v6): a box per entity in its category's color, a health bar for living ones, a name and distance
    // for mobs (through the same declutter pass as the players'). Call after DrawWorld() with the entities to show - the tracker's predictions, or the
    // raw list. Nothing is drawn unless Settings::showEntities is on.
    void DrawEntities(Gfx& g, const Settings& s, const Camera& cam, const std::vector<EntityState>& list);
    int EntitiesShown() const { return entShown_; } // of the last DrawEntities(): drawn
    int EntitiesSeen() const { return entSeen_; } // ... and in the list
    int EntityKindShown(int kind) const { return kind < 0 || kind >= kEntityKindCount ? 0 : entKindShown_[kind]; }
    // The performance graphs (bottom-left): frame time and data age as 240-frame line graphs with their summary text, and the jitter line.
    void DrawPerfPanel(Gfx& g, int winH, const perf::PerfHistory& h);
    // Where the nearest player's box centre is on the screen, for the jitter meter (the golden tests' "player" probe: in front by 1.5 m or more,
    // inside the viewport plus 10 %). False when no player qualifies.
    bool ProbeNearestPlayer(const Settings& s, const Camera& cam, const WorldSnapshot& snap, double& x, double& y) const;
    // Labels of the last DrawWorld() that found no free place (declutter on) - for the status text and the tests.
    int LabelsDropped() const { return placer_.Dropped(); }
    // Items of the last DrawWorld(): how many were drawn and how many the junk / tier / name filters hid (the HUD shows both).
    int ItemsShown() const { return itemsShown_; }
    int ItemsFiltered() const { return itemsFiltered_; }

private:
    void DrawGraph(Gfx& g, float x, float y, float w, float h, const perf::Ring<perf::PerfHistory::kN>& r, const std::string& caption);
    void DrawTracer(Gfx& g, const Settings& s, const Camera& cam, const Vec3& target, float dist, bool isFriend);
    void DrawPlayer(Gfx& g, const Settings& s, const Camera& cam, const PlayerState& p, float dist, bool isFriend);
    void DrawSkeleton(Gfx& g, const Settings& s, const Camera& cam, const PlayerState& p, float dist, bool isFriend);
    void DrawItem(Gfx& g, const Settings& s, const Camera& cam, const ItemState& it, float dist);
    // A player's name/distance are not drawn where the box is drawn: the box code queues them and FlushLabels() draws them once every box is
    // down, nearest first, so that a far label gives way to a near one (LabelLayout.h).
    struct LabelReq
    {
        const PlayerState* p;
        float dist, cx, top, bottom;
        bool isFriend;
    };
    void QueueLabels(const PlayerState& p, float dist, bool isFriend, float cx, float top, float bottom);
    void FlushLabels(Gfx& g, const Settings& s);
    void DrawLabels(Gfx& g, const Settings& s, const LabelReq& r);

    labels::Placer placer_;
    std::array<LabelReq, labels::Placer::kCapacity / 2> reqs_{}; // the queue is filled far -> near, like the boxes
    int nReq_ = 0;

    struct EntReq
    {
        const EntityState* e;
        float dist, cx, top, bottom;
    };
    std::array<EntReq, 128> entReq_{};
    int entShown_ = 0, entSeen_ = 0;
    int entKindShown_[kEntityKindCount] = {};

    itemfilter::Rules itemRules_; // ItemFilterList, parsed again only when the text changes
    std::string itemRulesSrc_;
    int itemsShown_ = 0, itemsFiltered_ = 0;
};
