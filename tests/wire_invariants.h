// What must be true of EVERY snapshot the parser returns, whatever the bytes were. Used by wire_validation_test and wire_fuzz_test.
#pragma once
#include "Wire.h"
#include <cmath>
#include <cstdio>
#include <string>

namespace wire_inv
{
    inline bool ValidUtf8NoControl(const std::string& s)
    {
        for (size_t i = 0; i < s.size();)
        {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (c < 0x20 || c == 0x7F) return false;
            if (c < 0x80) { ++i; continue; }
            size_t n = c >= 0xF0 ? 3 : (c >= 0xE0 ? 2 : (c >= 0xC2 ? 1 : 0));
            if (n == 0 || c > 0xF4 || i + n >= s.size()) return false;
            uint32_t cp = c & (0x3F >> n);
            for (size_t k = 1; k <= n; ++k)
            {
                const unsigned char cc = static_cast<unsigned char>(s[i + k]);
                if ((cc & 0xC0) != 0x80) return false;
                cp = (cp << 6) | (cc & 0x3F);
            }
            const uint32_t min = n == 1 ? 0x80 : (n == 2 ? 0x800 : 0x10000);
            if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
            i += n + 1;
        }
        return true;
    }

    // returns "" if fine, else a description of the first violation
    inline std::string Violation(const WorldSnapshot& s)
    {
        char b[200];
        auto fin = [&](float v, float lim, const char* what) -> std::string
        {
            if (!std::isfinite(v) || std::fabs(v) > lim) { std::snprintf(b, sizeof b, "%s = %g", what, static_cast<double>(v)); return b; }
            return {};
        };
        auto player = [&](const PlayerState& p, const char* who) -> std::string
        {
            std::string e;
            if (!(e = fin(p.pos.x, wire::kMaxCoord, "pos.x")).empty() || !(e = fin(p.pos.y, wire::kMaxCoord, "pos.y")).empty() || !(e = fin(p.pos.z, wire::kMaxCoord, "pos.z")).empty()) return std::string(who) + " " + e;
            if (p.yawValid && !(e = fin(p.yawDeg, wire::kMaxAngle, "yaw")).empty()) return std::string(who) + " " + e;
            if (p.pitchValid && !(e = fin(p.pitchDeg, wire::kMaxAngle, "pitch")).empty()) return std::string(who) + " " + e;
            if (p.healthValid && (!std::isfinite(p.health) || p.health < 0 || p.health > wire::kMaxHealth)) return std::string(who) + " health";
            if (p.sizeValid && (!std::isfinite(p.width) || !std::isfinite(p.height) || p.width <= 0 || p.height <= 0 || p.width > wire::kMaxBox || p.height > wire::kMaxBox)) return std::string(who) + " size";
            if (p.name.size() > wire::kMaxNameBytes || !ValidUtf8NoControl(p.name)) return std::string(who) + " name";
            if (p.uuid.size() > wire::kMaxUuidBytes || !ValidUtf8NoControl(p.uuid)) return std::string(who) + " uuid";
            return {};
        };
        std::string e;
        if (s.localValid && !(e = player(s.local, "local")).empty()) return e;
        if (s.players.size() > wire::kMaxPlayers) return "too many players";
        for (const auto& p : s.players) if (!(e = player(p, "player")).empty()) return e;
        if (s.items.size() > wire::kMaxItems) return "too many items";
        for (const auto& it : s.items)
        {
            if (!(e = fin(it.pos.x, wire::kMaxCoord, "item x")).empty() || !(e = fin(it.pos.y, wire::kMaxCoord, "item y")).empty() || !(e = fin(it.pos.z, wire::kMaxCoord, "item z")).empty()) return e;
            if (it.count < 1 || it.count > wire::kMaxCount) return "item count";
            if (it.sizeValid && (!std::isfinite(it.width) || !std::isfinite(it.height) || it.width <= 0 || it.height <= 0)) return "item size";
            if (it.key.size() > wire::kMaxKeyBytes || !ValidUtf8NoControl(it.key)) return "item key";
            if (it.tags.size() > wire::kMaxTagsBytes || !ValidUtf8NoControl(it.tags)) return "item tags";
            if (it.enchants.size() > wire::kMaxEnchants) return "too many enchants";
            for (const auto& en : it.enchants) if (!ValidUtf8NoControl(en.id) || en.level < -1000000 || en.level > 1000000) return "enchant";
        }
        const CameraState& c = s.camera;
        if (c.valid)
        {
            if (!(e = fin(c.pos.x, wire::kMaxCoord, "cam x")).empty() || !(e = fin(c.pos.y, wire::kMaxCoord, "cam y")).empty() || !(e = fin(c.pos.z, wire::kMaxCoord, "cam z")).empty()) return e;
            if (!(e = fin(c.yawDeg, wire::kMaxAngle, "cam yaw")).empty() || !(e = fin(c.pitchDeg, wire::kMaxAngle, "cam pitch")).empty()) return e;
            if (c.fovValid && !(c.fovDeg >= wire::kMinFov && c.fovDeg <= wire::kMaxFov)) return "cam fov";
            if (c.mode < 1 || c.mode > 3) return "cam mode";
        }
        if (s.status.size() > wire::kMaxStatusBytes || !ValidUtf8NoControl(s.status)) return "status";
        return {};
    }
} // namespace wire_inv
