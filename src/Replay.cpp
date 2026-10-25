#include "Replay.h"
#include "EspRec.h"
#include "Logger.h"
#include "Wire.h"
#include <algorithm>
#include <cmath>
#include <fstream>

bool ReplayPlayer::Load(const std::filesystem::path& file, std::string* error)
{
    auto fail = [&](const std::string& m)
    {
        if (error) *error = m;
        return false;
    };
    std::ifstream in(file, std::ios::binary);
    if (!in) return fail("cannot open " + file.string());
    esprec::Reader reader(in);
    esprec::Header h;
    if (const auto s = reader.ReadHeader(h); s != esprec::Status::Ok) return fail(file.string() + ": " + esprec::StatusText(s));
    recs_.clear();
    uint64_t t = 0, t0 = 0, bytes = 0;
    std::string payload;
    esprec::Status s;
    while ((s = reader.Next(t, payload)) == esprec::Status::Ok)
    {
        if (recs_.empty()) t0 = t;
        bytes += payload.size();
        if (bytes > (1ull << 30)) return fail(file.string() + ": larger than 1 GiB, refusing to load");
        recs_.push_back(Rec{t - t0, payload});
    }
    truncated_ = (s == esprec::Status::Truncated);
    if (s == esprec::Status::BadRecord) return fail(file.string() + ": " + esprec::StatusText(s));
    if (recs_.empty()) return fail(file.string() + ": contains no datagrams");
    skipped_ = reader.Skipped();
    wireVersion_ = h.wireVersion;
    const uint64_t last = recs_.back().t;
    const uint64_t gap = recs_.size() > 1 ? std::max<uint64_t>(1000000, last / (recs_.size() - 1)) : 8000000;
    loopLen_ = last + gap; // after the last datagram, wait one typical gap before the loop restarts
    next_ = 0;
    started_ = false;
    haveRef_ = false;
    loops_ = 0;
    if (truncated_) LOG_WARN("Replay: %s ends inside a record (the recording was cut off); %llu datagram(s) are usable.", file.string().c_str(),
                                 static_cast<unsigned long long>(recs_.size()));
    LOG_INFO("Replay: loaded %llu datagram(s), %.2f s, wire v%u from %s", static_cast<unsigned long long>(recs_.size()),
             static_cast<double>(last) / 1e9, static_cast<unsigned>(wireVersion_), file.string().c_str());
    return true;
}

void ReplayPlayer::Publish(StreamIngest& ingest, const Rec& r, Clock::time_point arrival)
{
    auto snap = std::make_shared<WorldSnapshot>();
    if (const ParseResult pr = ParseDatagramEx(r.payload.data(), r.payload.size(), *snap); pr.status != ParseStatus::Ok)
    {
        ingest.NoteRejected(pr);
        return;
    }
    if (snap->hasSampleTime)
    {
        if (!haveRef_)
        {
            haveRef_ = true;
            refT_ = static_cast<int64_t>(r.t);
            refSample_ = snap->sampleNs;
        }
        // recorded transport offset D = (t - t_ref) - (T - T_ref); replay keeps (arrival - T') = D / speed
        const double d = static_cast<double>(static_cast<int64_t>(r.t) - refT_) - static_cast<double>(snap->sampleNs - refSample_);
        const int64_t arrivalNs = std::chrono::duration_cast<std::chrono::nanoseconds>(arrival.time_since_epoch()).count();
        snap->sampleNs = arrivalNs - static_cast<int64_t>(std::llround(d / speed_));
    }
    ingest.Publish(std::move(snap), arrival);
}

size_t ReplayPlayer::Pump(StreamIngest& ingest, Clock::time_point now)
{
    if (recs_.empty()) return 0;
    if (!started_)
    {
        started_ = true;
        lastNow_ = now;
        playhead_ = 0;
    }
    if (!paused_) playhead_ += std::chrono::duration<double, std::nano>(now - lastNow_).count() * speed_;
    lastNow_ = now;
    size_t n = 0;
    for (;;)
    {
        if (next_ >= recs_.size())
        {
            if (!loop_ || paused_ || playhead_ < static_cast<double>(loopLen_)) break;
            playhead_ -= static_cast<double>(loopLen_);
            next_ = 0;
            ++loops_;
        }
        const Rec& r = recs_[next_];
        if (static_cast<double>(r.t) > playhead_) break;
        // the moment this datagram "should" have arrived on the real clock: now, minus how far the playhead is past it
        const auto lag = std::chrono::nanoseconds(static_cast<int64_t>((playhead_ - static_cast<double>(r.t)) / speed_));
        Publish(ingest, r, now - lag);
        ++next_;
        ++n;
    }
    return n;
}

bool ReplayPlayer::Step(StreamIngest& ingest, Clock::time_point now)
{
    if (recs_.empty() || !paused_) return false;
    if (!started_)
    {
        started_ = true;
        playhead_ = 0;
    }
    lastNow_ = now;
    if (next_ >= recs_.size())
    {
        if (!loop_) return false;
        next_ = 0;
        ++loops_;
    }
    const Rec& r = recs_[next_];
    playhead_ = static_cast<double>(r.t);
    Publish(ingest, r, now);
    ++next_;
    return true;
}

void ReplayDriver::Start()
{
    if (thread_.joinable()) return;
    speed_ = player_.Speed();
    paused_ = player_.Paused();
    thread_ = std::jthread([this](std::stop_token st)
    {
        using namespace std::chrono_literals;
        while (!st.stop_requested())
        {
            if (const double s = speedReq_.exchange(0)) player_.SetSpeed(s);
            if (togglePause_.exchange(false)) player_.SetPaused(!player_.Paused());
            const auto now = ReplayPlayer::Clock::now();
            if (step_.exchange(false)) player_.Step(ingest_, now);
            player_.Pump(ingest_, now);
            speed_ = player_.Speed();
            paused_ = player_.Paused();
            pos_ = player_.Position();
            finished_ = player_.Finished();
            std::this_thread::sleep_for(1ms);
        }
    });
}

void ReplayDriver::Stop()
{
    thread_.request_stop();
    if (thread_.joinable()) thread_.join();
}
