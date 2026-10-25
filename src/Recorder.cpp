#include "Recorder.h"
#include "EspRec.h"
#include "Logger.h"
#include <algorithm>
#include <cstring>
#include <fstream>

struct Recorder::File
{
    std::ofstream out;
};

namespace
{
    size_t RoundUpPow2(size_t n)
    {
        size_t p = 4096;
        while (p < n) p <<= 1;
        return p;
    }
} // namespace

Recorder::Recorder(size_t ringBytes) : cap_(RoundUpPow2(ringBytes)), mask_(cap_ - 1)
{
}

Recorder::~Recorder() { Stop(); }

bool Recorder::Start(const std::filesystem::path& file, Clock::time_point start, std::string* error)
{
    if (active_) return true;
    file_ = std::make_unique<File>();
    file_->out.open(file, std::ios::binary | std::ios::trunc);
    if (!file_->out)
    {
        if (error) *error = "cannot create " + file.string();
        file_.reset();
        return false;
    }
    ring_ = std::make_unique<unsigned char[]>(cap_);
    path_ = file;
    start_ = start;
    lastNs_ = 0;
    head_ = tail_ = 0;
    recorded_ = dropped_ = 0;
    failed_ = false;
    headerWritten_ = false;
    startUnixMs_ = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    active_.store(true, std::memory_order_release);
    thread_ = std::jthread([this](std::stop_token st) { WriterLoop(st); });
    LOG_INFO("Recorder: recording raw datagrams to %s (ring %llu KiB)", file.string().c_str(), static_cast<unsigned long long>(cap_ >> 10));
    return true;
}

bool Recorder::Record(const void* data, size_t len, Clock::time_point arrived) noexcept
{
    if (!active_.load(std::memory_order_acquire)) return false;
    const size_t need = esprec::kRecordHeaderSize + len;
    const uint64_t head = head_.load(std::memory_order_relaxed); // we are the only writer of head_
    const uint64_t tail = tail_.load(std::memory_order_acquire);
    if (len > esprec::kMaxRecordBytes || need > cap_ - (head - tail))
    {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    uint64_t ns = arrived > start_ ? static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(arrived - start_).count()) : 0;
    ns = std::max(ns, lastNs_);
    lastNs_ = ns;

    unsigned char hdr[esprec::kRecordHeaderSize];
    esprec::EncodeRecordHeader(ns, static_cast<uint32_t>(len), esprec::kKindDatagram, hdr);
    size_t pos = static_cast<size_t>(head) & mask_;
    auto put = [&](const unsigned char* src, size_t n)
    {
        const size_t first = std::min(n, cap_ - pos);
        std::memcpy(ring_.get() + pos, src, first);
        if (n > first) std::memcpy(ring_.get(), src + first, n - first);
        pos = (pos + n) & mask_;
    };
    put(hdr, sizeof hdr);
    if (len) put(static_cast<const unsigned char*>(data), len);
    head_.store(head + need, std::memory_order_release); // publish the complete frame
    recorded_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

// The first frame tells us which wire version the session speaks; peek at "MCRADAR <n>" for the header.
void Recorder::WriteHeaderIfNeeded(size_t firstFrameOffset, bool haveFrame)
{
    if (headerWritten_) return;
    headerWritten_ = true;
    esprec::Header h;
    h.startUnixMs = startUnixMs_;
    if (haveFrame)
    {
        char p[16] = {};
        for (size_t i = 0; i < sizeof p; ++i) p[i] = static_cast<char>(ring_[(firstFrameOffset + esprec::kRecordHeaderSize + i) & mask_]);
        if (std::memcmp(p, "MCRADAR ", 8) == 0 && p[8] >= '1' && p[8] <= '9') h.wireVersion = static_cast<uint32_t>(p[8] - '0');
    }
    esprec::Writer w(file_->out);
    if (!w.WriteHeader(h)) failed_ = true;
}

void Recorder::WriterLoop(std::stop_token st)
{
    auto lastFlush = Clock::now();
    for (;;)
    {
        const bool stopping = st.stop_requested();
        const uint64_t head = head_.load(std::memory_order_acquire);
        const uint64_t tail = tail_.load(std::memory_order_relaxed);
        const size_t avail = static_cast<size_t>(head - tail);
        if (avail > 0)
        {
            const size_t idx = static_cast<size_t>(tail) & mask_;
            WriteHeaderIfNeeded(idx, true);
            const size_t first = std::min(avail, cap_ - idx);
            if (!failed_)
            {
                file_->out.write(reinterpret_cast<const char*>(ring_.get() + idx), static_cast<std::streamsize>(first));
                if (avail > first) file_->out.write(reinterpret_cast<const char*>(ring_.get()), static_cast<std::streamsize>(avail - first));
                if (!file_->out)
                {
                    failed_ = true;
                    LOG_ERROR("Recorder: writing %s failed (disk full?) - the recording stops here.", path_.string().c_str());
                }
            }
            tail_.store(head, std::memory_order_release); // even when failing: keep draining so Record() never starts blocking
        }
        else if (stopping) break;
        if (Clock::now() - lastFlush > std::chrono::milliseconds(250))
        {
            file_->out.flush(); // a crash loses at most ~250 ms
            lastFlush = Clock::now();
        }
        if (avail == 0) std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    WriteHeaderIfNeeded(0, false); // nothing was ever recorded: still a valid (empty) file
}

void Recorder::Stop()
{
    if (!active_.exchange(false, std::memory_order_acq_rel)) return;
    thread_.request_stop();
    if (thread_.joinable()) thread_.join();
    if (file_)
    {
        file_->out.flush();
        file_->out.close();
    }
    LOG_INFO("Recorder: stopped; %llu datagram(s) recorded, %llu dropped%s -> %s", static_cast<unsigned long long>(Recorded()),
             static_cast<unsigned long long>(Dropped()), failed_ ? ", WRITE FAILED" : "", path_.string().c_str());
    if (Dropped() > 0) LOG_WARN("Recorder: %llu datagram(s) did not fit the ring buffer (the disk was too slow).", static_cast<unsigned long long>(Dropped()));
    file_.reset();
}
