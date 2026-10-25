// Golden-test pipeline: .esprec -> ReplayPlayer -> StreamIngest -> Smoother -> Camera projection, on a FAKE clock, measuring how much
// projected points jitter on screen. Mirrors EspOverlay::RenderFrame (push the newest snapshot once, PredictInto, camera from the
// agent's exact camera else reconstructed), minus drawing.
//
// Jitter metric (pixels): for consecutive frames i-1, i, i+1 the deviation of frame i from the midpoint of its neighbours,
//     dev_i = | p_i - (p_{i-1} + p_{i+1}) / 2 |,
// i.e. half the second difference of the on-screen position. Smooth motion (constant or slowly changing velocity) gives ~0; a box that
// stutters or vibrates around a smoothly moving target gives a large value. It needs no ground truth, so it works on real recordings too.
// Reported: RMS, 95th percentile and maximum of dev over the measured frames, plus the largest frame-to-frame step.
#pragma once
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace golden
{
    struct Options
    {
        double frameHz = 240; // virtual display refresh the smoother is sampled at
        int width = 1920, height = 1080;
        double warmupSec = 1.0; // the smoother's clock-offset estimate needs a moment; frames before this are not measured
        double smoothing = 1; // Settings::smoothing is a bool: 1 = on
    };

    struct Metrics
    {
        double frames = 0; // frames that went into the metric
        double jitterRmsPx = 0, jitterP95Px = 0, jitterMaxPx = 0;
        double maxStepPx = 0; // largest frame-to-frame movement
    };

    using Result = std::map<std::string, Metrics>; // probe name -> metrics

    // The metric itself, for a track of on-screen positions (NaN = not visible that frame). Frames before `firstMeasured` are skipped.
    Metrics Jitter(const std::vector<std::pair<double, double>>& px, size_t firstMeasured = 0);

    // Probes: "world_anchor" (a fixed world point 500 m in front of the first camera: effectively at infinity, so its screen motion is
    // the camera's ROTATION) and "player:<name>" (box centre of every remote player). A probe is measured only on frames where the point
    // is in front of the camera by >= 1.5 m and within 10 % of the viewport edge: perspective makes the pixel speed of a point blow up
    // toward the edge of a wide FOV and near the camera plane, and that is geometry, not smoothing quality.
    // false + *error if the recording cannot be replayed.
    // Optional `tracks`: the per-frame screen positions behind the metrics (NaN = not measured that frame), for plotting / debugging.
    using Tracks = std::map<std::string, std::vector<std::pair<double, double>>>;
    bool Measure(const std::string& esprecPath, const Options& opt, Result& out, std::string* error, Tracks* tracks = nullptr);

    // Flat JSON, one number per key: "probe/<name>/<metric>", plus the options used.
    std::string ToJson(const std::string& recordingName, const Options& opt, const Result& r);
    bool FromJson(const std::string& text, Options& opt, Result& r, std::string* error);

    struct Tolerance
    {
        double relative = 0.10; // allowed worsening of every jitter metric, relative to the golden value
        double absolutePx = 0.05; // ...plus this many pixels (so a golden of 0.000 does not make 0.001 a failure)
        double stepAbsolutePx = 1.0; // max_step is allowed this much more
        double frameRelative = 0.01; // the number of measured frames may differ by this much
    };

    // Compares a measurement with the golden numbers. Only WORSENING beyond the tolerance fails; improvements are reported in
    // *notes so goldens can be regenerated on purpose. Returns the failures as text lines (empty = pass).
    std::string Compare(const Result& golden, const Result& measured, const Tolerance& tol, std::string* notes);
} // namespace golden
