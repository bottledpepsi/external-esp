// Replay.h - plays an .esprec recording back into a StreamIngest, for --replay and for the golden tests.
//
//   ReplayPlayer  deterministic, no threads, no clock of its own: the caller says what time it is (Pump(now)). Tests drive it
//                 with a fake clock; the overlay drives it from ReplayDriver.
//   ReplayDriver  a small thread that calls Pump(steady_clock::now()) every millisecond and takes pause / step / speed requests.
//
// What is replayed: every datagram, in order, at its recorded arrival time (scaled by the speed), through the same parser and the
// same Publish() the live socket thread uses. Differences from live, on purpose:
//   * no token check (a recording was authenticated when it was made),
//   * the agent's sample stamp T is rewritten so that (arrival - T) keeps the recorded transport jitter at 1x and stays consistent
//     at other speeds and across loops; the Smoother maps the agent's clock onto ours from exactly that difference.
// Each datagram is published individually, so a replay never skips the ones the live "newest wins" drain would have skipped.
#pragma once
#include "StreamIngest.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

class ReplayPlayer
{
public:
    using Clock = StreamIngest::Clock;

    // Reads the whole file into memory (a few MB per minute). A recording that was cut off loads up to its last complete
    // record and sets Truncated(). false + *error if it cannot be used at all (missing, not an .esprec, empty).
    bool Load(const std::filesystem::path& file, std::string* error = nullptr);

    void SetSpeed(double speed) { speed_ = speed < 0.05 ? 0.05 : (speed > 20.0 ? 20.0 : speed); }
    void SetLoop(bool loop) { loop_ = loop; }
    void SetPaused(bool paused) { paused_ = paused; }
    double Speed() const { return speed_; }
    bool Paused() const { return paused_; }

    // Advance the playhead to `now` (frozen while paused) and publish every datagram that is due. Returns how many.
    // The first call defines "time zero of the replay".
    size_t Pump(StreamIngest& ingest, Clock::time_point now);
    // While paused: publish exactly the next datagram (and move the playhead to it).
    bool Step(StreamIngest& ingest, Clock::time_point now);

    size_t Count() const { return recs_.size(); }
    size_t Position() const { return next_; } // datagrams published in this pass
    uint64_t DurationNs() const { return loopLen_; }
    bool Finished() const { return !loop_ && next_ >= recs_.size(); }
    bool Truncated() const { return truncated_; }
    uint64_t Skipped() const { return skipped_; } // records of an unknown kind in the file
    uint32_t WireVersion() const { return wireVersion_; }
    uint64_t Loops() const { return loops_; }

private:
    struct Rec
    {
        uint64_t t; // ns since the first record
        std::string payload;
    };
    void Publish(StreamIngest& ingest, const Rec& r, Clock::time_point arrival);

    std::vector<Rec> recs_;
    uint64_t loopLen_ = 0;
    uint32_t wireVersion_ = 0;
    bool truncated_ = false;
    uint64_t skipped_ = 0;

    double speed_ = 1.0;
    bool loop_ = false, paused_ = false, started_ = false;
    Clock::time_point lastNow_{};
    double playhead_ = 0; // ns of recording time
    size_t next_ = 0;
    uint64_t loops_ = 0;
    // reference pair for rewriting the agent's sample stamp: recorded (t, T) of the first datagram that had one
    bool haveRef_ = false;
    int64_t refT_ = 0, refSample_ = 0;
};

class ReplayDriver
{
public:
    ReplayDriver(ReplayPlayer& player, StreamIngest& ingest) : player_(player), ingest_(ingest)
    {
    }

    ~ReplayDriver() { Stop(); }
    ReplayDriver(const ReplayDriver&) = delete;
    ReplayDriver& operator=(const ReplayDriver&) = delete;

    void Start(); // the player must be loaded and configured; it belongs to the driver thread from now on
    void Stop();

    // any thread
    void TogglePause() { togglePause_ = true; }
    void RequestStep() { step_ = true; }
    void SetSpeed(double s) { speedReq_ = s; }
    bool Paused() const { return paused_.load(); }
    double Speed() const { return speed_.load(); }
    size_t Position() const { return pos_.load(); }
    bool Finished() const { return finished_.load(); }

private:
    ReplayPlayer& player_;
    StreamIngest& ingest_;
    std::atomic<bool> togglePause_{false}, step_{false}, paused_{false}, finished_{false};
    std::atomic<double> speedReq_{0}, speed_{1.0};
    std::atomic<size_t> pos_{0};
    std::jthread thread_;
};
