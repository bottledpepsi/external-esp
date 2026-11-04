// EspRenderer.h - draws the ESP (boxes / skeletons / tracers / labels / health / items) and the minimap.
// Port of the drawing half of EspOverlay.cs; the projection comes from Camera.h instead of GL matrices.
#pragma once
#include "Camera.h"
#include "EspConfig.h"
#include "Gfx.h"
#include "LabelLayout.h"
#include "Types.h"
#include <array>

class EspRenderer
{
public:
    void DrawWorld(Gfx& g, const Settings& s, const Camera& cam, const WorldSnapshot& snap);
    void DrawMinimap(Gfx& g, const Settings& s, const Camera& cam, const WorldSnapshot& snap, int winW);
    // Labels of the last DrawWorld() that found no free place (declutter on) - for the status text and the tests.
    int LabelsDropped() const { return placer_.Dropped(); }

private:
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
};
