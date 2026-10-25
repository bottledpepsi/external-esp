#include "StreamIngest.h"
#include "Wire.h"

bool StreamIngest::OnDatagram(const char* data, size_t len, Clock::time_point arrived)
{
    auto snap = std::make_shared<WorldSnapshot>();
    if (!ParseDatagram(data, len, *snap))
    {
        NoteRejected();
        return false;
    }
    Publish(std::move(snap), arrived);
    return true;
}

void StreamIngest::Publish(std::shared_ptr<WorldSnapshot> snap, Clock::time_point arrived)
{
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
