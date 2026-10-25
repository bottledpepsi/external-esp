#include "StreamIngest.h"
#include "Logger.h"
#include "Wire.h"
#include <cstdio>

bool StreamIngest::OnDatagram(const char* data, size_t len, Clock::time_point arrived)
{
    auto snap = std::make_shared<WorldSnapshot>();
    const ParseResult r = ParseDatagramEx(data, len, *snap);
    if (r.status != ParseStatus::Ok)
    {
        NoteRejected(r);
        return false;
    }
    Publish(std::move(snap), arrived);
    return true;
}

void StreamIngest::NoteRejected(const ParseResult& why)
{
    ++rejected_;
    if (why.status != ParseStatus::UnsupportedVersion) return;
    if (unsupportedVersion_.exchange(why.version) != why.version)
        LOG_WARN("Agent speaks wire protocol v%d but this overlay understands v%d..v%d: rebuild mcesp.exe and esp-agent.jar together from the same source.", why.version,
                 wire::kMinVersion, wire::kMaxVersion);
    unsupportedNs_ = now_().time_since_epoch().count(); // the injected clock, so tests and the overlay agree on "now"
}

std::string StreamIngest::ProtocolProblem(Clock::time_point now) const
{
    const int64_t window = std::chrono::duration_cast<Clock::duration>(std::chrono::seconds(3)).count();
    const int64_t n = now.time_since_epoch().count();
    const bool unsupportedRecent = unsupportedVersion_.load() != 0 && n - unsupportedNs_.load() <= window;
    const bool acceptedRecent = have_.load() && n - lastNs_.load() <= window;
    char b[220];
    if (unsupportedRecent && !acceptedRecent)
    {
        std::snprintf(b, sizeof b, "agent speaks wire protocol v%d, this overlay reads v%d-v%d: rebuild mcesp.exe and esp-agent.jar together", unsupportedVersion_.load(),
                      wire::kMinVersion, wire::kMaxVersion);
        return b;
    }
    const int v = acceptedVersion_.load();
    if (acceptedRecent && v != 0 && v < wire::kMaxVersion)
    {
        std::snprintf(b, sizeof b, "agent speaks the old wire protocol v%d (this overlay: v%d): rebuild esp-agent.jar - no sample timestamps, smoothing is degraded", v,
                      wire::kMaxVersion);
        return b;
    }
    return {};
}

void StreamIngest::Publish(std::shared_ptr<WorldSnapshot> snap, Clock::time_point arrived)
{
    if (snap->protocolVersion != 0) acceptedVersion_ = snap->protocolVersion;
    snap->seq = ++seq_;
    snap->arrived = arrived;
    lastNs_ = arrived.time_since_epoch().count();
    have_ = true;
    ++count_;
    std::lock_guard<std::mutex> lk(mtx_); // pointer swap only
    latest_ = std::move(snap);
}

std::shared_ptr<const WorldSnapshot> StreamIngest::Latest() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return latest_;
}

StreamIngest::Clock::time_point StreamIngest::LastPacketTime() const
{
    return have_.load() ? Clock::time_point(Clock::duration(lastNs_.load())) : Clock::time_point{};
}
