// Camera.h - coordinate projection: world position -> overlay pixel.
//
// The original C# tool read OpenGL modelview/projection matrices from its agent. This agent works by
// reading game objects (version independent) and has no GL hook, so the camera is RECONSTRUCTED
// from what it does know: the local player's eye position, yaw and pitch, plus the vertical FOV
// (Minecraft's FOV option is the vertical field of view) and the window's aspect ratio.
#pragma once
#include "Types.h"
#include <cmath>

struct Camera
{
    Vec3 eye;
    double yawDeg = 0, pitchDeg = 0, fovYDeg = 70;
    int w = 1920, h = 1080;

    // Derived basis (call Update() after changing the inputs).
    Vec3 f, r, u;
    double k = 1, aspect = 1; // k = 1 / tan(fovY / 2)

    static constexpr double kEyeHeight = 1.62; // standing eye height above the feet position
    static constexpr double kNear = 0.05;

    void Update()
    {
        const double rad = 3.14159265358979323846 / 180.0;
        const double yaw = yawDeg * rad, pitch = pitchDeg * rad;
        const double sy = std::sin(yaw), cy = std::cos(yaw), sp = std::sin(pitch), cp = std::cos(pitch);
        // Minecraft: forward = (-sin(yaw)cos(pitch), -sin(pitch), cos(yaw)cos(pitch)); pitch +90 looks DOWN.
        f = {-sy * cp, -sp, cy * cp};
        r = {-cy, 0.0, -sy}; // player's right-hand side
        // up = right x forward
        u = {r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x};
        k = 1.0 / std::tan(fovYDeg * rad * 0.5);
        aspect = h > 0 ? static_cast<double>(w) / h : 1.0;
    }

    static double Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

    // Camera-space coordinates of a world point.
    void ToCamera(const Vec3& p, double& xc, double& yc, double& zc) const
    {
        const Vec3 d{p.x - eye.x, p.y - eye.y, p.z - eye.z};
        xc = Dot(d, r);
        yc = Dot(d, u);
        zc = Dot(d, f);
    }

    // Perspective divide. Returns false if the point is behind the near plane.
    bool Project(const Vec3& p, float& sx, float& sy) const
    {
        double xc, yc, zc;
        ToCamera(p, xc, yc, zc);
        if (zc < kNear) return false;
        const double nx = xc / zc * k / aspect; // NDC x in [-1, 1] across the screen width
        const double ny = yc / zc * k; // NDC y in [-1, 1] across the screen height
        sx = static_cast<float>((nx + 1.0) * 0.5 * w);
        sy = static_cast<float>((1.0 - ny) * 0.5 * h);
        return true;
    }

    // Like Project(), but a point behind the near plane is clamped to near (used for box corners that
    // straddle the camera plane so a close player still gets a box).
    void ProjectClamped(const Vec3& p, float& sx, float& sy) const
    {
        double xc, yc, zc;
        ToCamera(p, xc, yc, zc);
        if (zc < kNear) zc = kNear;
        sx = static_cast<float>((xc / zc * k / aspect + 1.0) * 0.5 * w);
        sy = static_cast<float>((1.0 - yc / zc * k) * 0.5 * h);
    }

    double Distance(const Vec3& p) const
    {
        const double dx = p.x - eye.x, dy = p.y - eye.y, dz = p.z - eye.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }
};

// The camera a snapshot describes, by the same rules the overlay uses for its main view: the game's own camera when the agent found it
// (exact pose, and its FOV when it has one), else the local player's eye and angles with `fallbackFovDeg`. Returns false when the snapshot has
// neither. `exact` (optional) says which of the two it was. Used for the "raw" view, which needs a camera of its own.
inline bool CameraFromSnapshot(const WorldSnapshot& s, double fallbackFovDeg, int w, int h, Camera& out, bool* exact = nullptr)
{
    if (s.camera.valid)
    {
        const CameraState& c = s.camera;
        out.eye = c.pos;
        out.yawDeg = c.yawDeg;
        out.pitchDeg = c.pitchDeg;
        out.fovYDeg = c.fovValid ? c.fovDeg : fallbackFovDeg;
        if (exact) *exact = true;
    }
    else if (s.localValid && s.local.yawValid && s.local.pitchValid)
    {
        out.eye = {s.local.pos.x, s.local.pos.y + Camera::kEyeHeight, s.local.pos.z};
        out.yawDeg = s.local.yawDeg;
        out.pitchDeg = s.local.pitchDeg;
        out.fovYDeg = fallbackFovDeg;
        if (exact) *exact = false;
    }
    else return false;
    out.w = w;
    out.h = h;
    out.Update();
    return true;
}
