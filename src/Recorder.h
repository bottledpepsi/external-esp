// Recorder.h - tees the raw UDP datagrams (plus arrival times) into an .esprec file (docs/esprec.md) for --record.
//
// Built for the hot path: Record() is called by the UDP thread for EVERY datagram, so it
//   - never allocates (the ring buffer is allocated once in Start()),
//   - never takes a lock and never does file I/O,
//   - never blocks: if the ring is full the datagram is dropped and counted, the game/overlay is never slowed down.
// A separate writer thread drains the ring to the file. The ring holds the exact bytes that go into the file (16-byte
// record header + payload), so the writer thread just copies ranges to the stream.
//
// Threading contract: exactly ONE thread calls Record() (the UDP thread); any thread may read the counters. Stop() must be
// called after that thread has stopped (EspOverlay stops the UdpListener first); Record() after Stop() returns false.
#pragma once
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

class Recorder
{
public:
    using Clock = std::chrono::steady_clock;
    static constexpr size_t kDefaultRingBytes = 8u << 20; // ~60 s of datagrams if the disk stalls completely

    // `ringBytes` is rounded up to a power of two (minimum 4 KiB).
    explicit Recorder(size_t ringBytes = kDefaultRingBytes);
    ~Recorder();
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    // Creates/truncates `file` and starts the writer thread. Times in the file are relative to `start`.
    bool Start(const std::filesystem::path& file, Clock::time_point start, std::string* error = nullptr);
    bool Start(const std::filesystem::path& file, std::string* error = nullptr) { return Start(file, Clock::now(), error); }

    // UDP thread only. true = queued, false = dropped (ring full, frame too big, or not recording).
    bool Record(const void* data, size_t len, Clock::time_point arrived) noexcept;

    void Stop(); // drains the ring, flushes and closes the file; safe to call twice
    bool Active() const { return active_.load(std::memory_order_acquire); }
    uint64_t Recorded() const { return recorded_.load(std::memory_order_relaxed); } // datagrams queued
    uint64_t Dropped() const { return dropped_.load(std::memory_order_relaxed); } // datagrams that did not fit
    bool Failed() const { return failed_.load(); } // a write to the file failed (disk full...)
    const std::filesystem::path& Path() const { return path_; }

private:
    void WriterLoop(std::stop_token st);
    void WriteHeaderIfNeeded(size_t firstFrameOffset, bool haveFrame);

    struct File; // std::ofstream, kept out of the header
    size_t cap_, mask_;
    std::unique_ptr<unsigned char[]> ring_;
    alignas(64) std::atomic<uint64_t> head_{0}; // total bytes published by the producer
    alignas(64) std::atomic<uint64_t> tail_{0}; // total bytes consumed by the writer
    std::atomic<bool> active_{false}, failed_{false};
    std::atomic<uint64_t> recorded_{0}, dropped_{0};
    Clock::time_point start_{};
    uint64_t lastNs_ = 0; // producer thread only (keeps t_ns non-decreasing)
    int64_t startUnixMs_ = 0;
    bool headerWritten_ = false;
    std::filesystem::path path_;
    std::unique_ptr<File> file_;
    std::jthread thread_;
};
