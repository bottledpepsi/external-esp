// StreamIngest.h - everything that happens to a datagram AFTER it has been received, with no socket and no Windows API:
// parse it, stamp it (sequence number, arrival time) and publish it to the render thread.
//
// Split out of UdpListener so that the same code serves three sources: the live UDP thread, a recording being replayed
// (--replay) and tests that drive it with a fake clock. Threading is unchanged from the original UdpListener: ONE producer
// thread publishes, any other thread reads. The hand-off is a shared_ptr swap under a mutex held only for the pointer copy;
// the published WorldSnapshot is immutable, so the renderer reads it without a lock.
#pragma once
#include "Types.h"
#include "Wire.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

class StreamIngest
{
public:
    using Clock = std::chrono::steady_clock;
    using NowFn = std::function<Clock::time_point()>;

    // `now` is the time source used by OnDatagram(data, len); default steady_clock::now. Tests pass a fake clock.
    explicit StreamIngest(NowFn now = {}) : now_(now ? std::move(now) : NowFn(&Clock::now))
    {
    }

    StreamIngest(const StreamIngest&) = delete;
    StreamIngest& operator=(const StreamIngest&) = delete;

    // Parse one datagram and, if valid, publish it. Returns false (and counts it) if it is not a valid datagram.
    bool OnDatagram(const char* data, size_t len) { return OnDatagram(data, len, now_()); }
    bool OnDatagram(const char* data, size_t len, Clock::time_point arrived);

    // Publish an already parsed snapshot (the live socket thread drains the socket and publishes only the newest valid one).
    // Assigns seq (1, 2, 3, ...) and `arrived`.
    void Publish(std::shared_ptr<WorldSnapshot> snap, Clock::time_point arrived);

    // ---- session token (wire v5, Token.h) ----
    // With require = true every datagram must carry `token` in its K line; others are dropped (counted, never published). The default
    // (require = false) accepts everything, which is what replay and the tests want. Call before the producer thread starts.
    enum class Auth { Ok, NoToken, BadToken };
    void SetAuth(bool require, std::string token)
    {
        requireToken_ = require;
        token_ = std::move(token);
    }

    bool AuthRequired() const { return requireToken_; }
    Auth Authenticate(const WorldSnapshot& snap) const; // Ok when not required
    void NoteAuthFailure(Auth why, int version); // counts it; remembers why for the HUD message
    uint64_t AuthFailures() const { return authFailures_.load(); }

    // ---- datagram numbering (wire v5 Q line) ----
    // The agent numbers its datagrams within a session. NoteSequence() is called for EVERY authenticated datagram - also the ones the live
    // socket loop will not publish because a newer one is already waiting - so gaps mean real loss. Returns false for a datagram that
    // must be dropped: a duplicate, or one that arrives after a newer one (publishing it would move the world back in time).
    // A different session id means the agent restarted: the counters of the old session stay, numbering starts over.
    // Datagrams without a Q line (v1-v4) are always accepted. Replay turns the check off (a looping recording restarts its numbers).
    bool NoteSequence(const WorldSnapshot& snap);
    void SetSeqCheck(bool on) { seqCheck_ = on; }

    struct LinkStats
    {
        uint64_t numbered = 0; // datagrams that carried a number
        uint64_t lost = 0; // numbers that never arrived (corrected when a late one shows up)
        uint64_t reordered = 0; // arrived after a newer one: dropped
        uint64_t duplicates = 0; // the same number twice: dropped
        uint64_t sessions = 0; // agent (re)starts seen
    };

    LinkStats Link() const;
    std::string LinkSummary() const; // "" while nothing is wrong, else e.g. "UDP: 3 lost, 1 reordered of 12000"

    // A datagram that did not parse. `why` says whether it was a stranger or an agent of another protocol version.
    void NoteRejected(const ParseResult& why = {});
    void NoteRejected(ParseStatus status, int version) { NoteRejected(ParseResult{status, version}); }
    Clock::time_point Now() const { return now_(); }

    std::shared_ptr<const WorldSnapshot> Latest() const; // never blocks on I/O
    Clock::time_point LastPacketTime() const; // time_point{} if nothing yet
    uint64_t PacketCount() const { return count_.load(); } // snapshots published
    uint64_t RejectedCount() const { return rejected_.load(); } // datagrams that did not parse

    // "" when the agent and this overlay understand each other, else a one-line explanation for the HUD:
    //  - an agent announced a protocol version this build cannot read (and nothing readable came in since), or
    //  - the agent is readable but older than kMaxVersion (no timestamps: the smoothing falls back to arrival times).
    // `now` is the time to judge "recently" against (3 s); pass the same clock as the datagrams were stamped with.
    std::string ProtocolProblem(Clock::time_point now) const;
    int UnsupportedVersionSeen() const { return unsupportedVersion_.load(); } // the last unsupported version, 0 = none yet
    int AcceptedVersion() const { return acceptedVersion_.load(); } // the version of the last readable datagram, 0 = none yet

private:
    NowFn now_;
    bool seqCheck_ = true;
    uint32_t seqSession_ = 0, seqLast_ = 0; // producer thread only
    bool seqHave_ = false;
    std::atomic<uint64_t> numbered_{0}, lost_{0}, reordered_{0}, duplicates_{0}, sessions_{0};
    bool requireToken_ = false;
    std::string token_;
    std::atomic<uint64_t> authFailures_{0};
    std::atomic<int> authWhy_{0}, authVersion_{0}; // Auth value (1 NoToken / 2 BadToken) and the protocol version of the last failure
    std::atomic<int64_t> authNs_{0};
    uint64_t seq_ = 0; // producer thread only
    std::atomic<bool> have_{false};
    std::atomic<int64_t> lastNs_{0};
    std::atomic<uint64_t> count_{0}, rejected_{0};
    // which warnings were already logged: bit v of unsupportedLogged_ = "unsupported version v", bit (why*8+min(v,7)) of authLogged_ = one line
    // per (reason, version) however the failures interleave, so a hostile or mixed stream cannot flood the log
    std::atomic<uint64_t> unsupportedLogged_{0};
    std::atomic<uint32_t> authLogged_{0};
    std::atomic<int> unsupportedVersion_{0}, acceptedVersion_{0};
    std::atomic<int64_t> unsupportedNs_{0}, acceptedNs_{0};
    mutable std::mutex mtx_;
    std::shared_ptr<const WorldSnapshot> latest_;
};
