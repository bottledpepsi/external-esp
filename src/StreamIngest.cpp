#include "StreamIngest.h"
#include "Logger.h"
#include "Token.h"
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
    if (const Auth a = Authenticate(*snap); a != Auth::Ok)
    {
        NoteAuthFailure(a, snap->protocolVersion);
        return false;
    }
    Publish(std::move(snap), arrived);
    return true;
}

StreamIngest::Auth StreamIngest::Authenticate(const WorldSnapshot& snap) const
{
    if (!requireToken_) return Auth::Ok;
    if (snap.token.empty()) return Auth::NoToken; // v1-v4 agents and v5 datagrams without a K line
    return token::Equals(snap.token, token_) ? Auth::Ok : Auth::BadToken;
}

void StreamIngest::NoteAuthFailure(Auth why, int version)
{
    ++rejected_;
    ++authFailures_;
    const int w = why == Auth::NoToken ? 1 : 2;
    authWhy_ = w;
    authVersion_ = version;
    const uint32_t bit = 1u << (w * 8 + (version < 0 ? 0 : (version > 7 ? 7 : version)));
    if ((authLogged_.fetch_or(bit) & bit) == 0)
    {
        if (w == 1)
            LOG_WARN("Datagram without a session token (wire v%d) dropped: this overlay requires one. Rebuild esp-agent.jar together with mcesp.exe, or start with --no-auth.", version);
        else
            LOG_WARN("Datagram with a WRONG session token dropped: another program is sending to the port, or the agent is left over from another overlay session.");
    }
    authNs_ = now_().time_since_epoch().count();
}

void StreamIngest::NoteRejected(const ParseResult& why)
{
    ++rejected_;
    if (why.status != ParseStatus::UnsupportedVersion) return;
    const uint64_t bit = 1ull << (why.version < 0 ? 0 : (why.version > 63 ? 63 : why.version));
    unsupportedVersion_ = why.version;
    if ((unsupportedLogged_.fetch_or(bit) & bit) == 0)
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
    const bool authRecent = authWhy_.load() != 0 && n - authNs_.load() <= window;
    if (authRecent && !acceptedRecent)
    {
        if (authWhy_.load() == 1)
        {
            std::snprintf(b, sizeof b, "agent sends no session token (wire v%d): rebuild esp-agent.jar together with mcesp.exe, or run mcesp --no-auth", authVersion_.load());
            return b;
        }
        return "datagrams with a wrong session token are being ignored (another program on the port, or an agent from another session)";
    }
    const int v = acceptedVersion_.load();
    if (acceptedRecent && v != 0 && v < wire::kTimestampVersion)
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
