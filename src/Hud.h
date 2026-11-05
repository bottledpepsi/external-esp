// Hud.h - the text of the on-screen status lines. Portable; the numbers come in as Facts, the strings come out as lines.
//
//   Off       nothing (the exit countdown still shows - holding End must be visible)
//   Compact   one line while everything is fine:  external-esp | active | 262 fps | 118 Hz | age 6 ms | loss 0%
//             a second line only when something needs attention (waiting for the game, UDP loss, a protocol problem)
//   Detailed  the full picture: render times, the agent's rate, data age, jitter, loss and reordering, camera mode, recording
//
// Build() writes into strings that keep their capacity, and the overlay calls it four times a second, not every frame: the draw call then gets the
// same text again and again and the text-layout cache (Gfx) hits. No allocation after the first call (tests/hud_test.cpp counts them).
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace hud
{
    enum class Mode { Off = 0, Compact = 1, Detailed = 2 };
    const char* Name(Mode m); // "Off", "Compact", "Detailed"
    Mode Next(Mode m);
    bool Parse(const std::string& text, Mode& out);

    enum class Cam { None, Reconstructed, Exact, ExactThirdPerson };

    struct Facts
    {
        // the overlay
        bool visible = true, demo = false, replay = false, gpu = true;
        int view = 0; // 0 smoothed (the normal picture), 1 raw, 2 both: shown in the first line when it is not 0
        float fps = 0, cpuMs = 0;
        bool exiting = false;
        float exitLeftS = 0;
        // the agent link
        bool agentLive = false; // data is arriving
        bool haveCam = false;
        float sampleHz = 0; // snapshots per second that reached the overlay
        float ageMs = -1; // how old the newest sample is when a frame is drawn (-1 = unknown)
        float jitterMs = -1; // how much later than its best case a sample arrives, on average (-1 = unknown)
        float lossPct = 0; // datagrams the agent numbered that never arrived, over the last window
        uint64_t lostTotal = 0, reorderedTotal = 0;
        float predictionErrPx = -1; // the Smoother's guess error at each new sample, in pixels (-1 = unknown)
        Cam cam = Cam::None;
        // the sentences
        std::string_view status; // what the supervisor says ("agent connected", "waiting for minecraft", ...) or a protocol problem / replay line
        std::string_view link; // LinkSummary(): "" while nothing is wrong
        std::string_view snapStatus; // the agent's own one-liner
        bool recording = false;
        uint64_t recorded = 0, dropped = 0;
        // the overlay's own cost (Detailed only; perfValid = false leaves the line out)
        bool perfValid = false;
        float cacheHitPct = -1; // text-layout cache hits over the last window (-1 = no lookups)
        uint64_t cacheClears = 0; // times the cache was emptied because it was full (a high number = labels change every frame)
        int labelsDropped = 0; // labels the declutter pass found no room for in the last frame
        int entShown = -1, entSeen = 0; // mobs drawn / in the list; -1 = the entity display is off
        int itemsShown = 0, itemsFiltered = 0; // items drawn / hidden by the junk, tier and name filters in the last frame
    };

    class Text
    {
    public:
        Text();
        void Build(Mode mode, const Facts& f);
        int Lines() const { return lines_; }
        const std::string& Line(int i) const { return line_[i]; }
        bool Alert(int i) const { return alert_[i]; } // draw this line in the warning colour
        // the longest line a mode can produce is well under this many characters (the window is a few hundred pixels wide)
        static constexpr int kMaxLines = 5;

    private:
        void Set(int i, const char* s, size_t n, bool alert);
        std::string line_[kMaxLines];
        bool alert_[kMaxLines] = {};
        int lines_ = 0;
    };
} // namespace hud
