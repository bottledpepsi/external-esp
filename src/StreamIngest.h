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
    uint64_t seq_ = 0; // producer thread only
    std::atomic<bool> have_{false};
    std::atomic<int64_t> lastNs_{0};
    std::atomic<uint64_t> count_{0}, rejected_{0};
    std::atomic<int> unsupportedVersion_{0}, acceptedVersion_{0};
    std::atomic<int64_t> unsupportedNs_{0}, acceptedNs_{0};
    mutable std::mutex mtx_;
    std::shared_ptr<const WorldSnapshot> latest_;
};
