#include <winsock2.h>
#include "UdpListener.h"
#include "Logger.h"
#include "Wire.h"
#include <vector>

bool UdpListener::Start()
{
    if (running_) return true;
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        LOG_ERROR("WSAStartup failed.");
        return false;
    }
    wsa_ = true;

    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
    {
        LOG_ERROR("socket() failed (%d).", WSAGetLastError());
        return false;
    }
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port_);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // loopback only - never reachable from the network
    if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a) == SOCKET_ERROR)
    {
        LOG_ERROR("Cannot listen on 127.0.0.1:%u (error %d). Is another instance running? Use --port.",
                  static_cast<unsigned>(port_), WSAGetLastError());
        closesocket(s);
        return false;
    }
    sock_ = static_cast<uintptr_t>(s);
    running_ = true;
    thread_ = std::jthread([this](std::stop_token st) { Loop(st); });
    LOG_INFO("UdpListener: listening on udp://127.0.0.1:%u", static_cast<unsigned>(port_));
    return true;
}

void UdpListener::Stop()
{
    if (thread_.joinable())
    {
        thread_.request_stop();
        thread_.join();
    }
    running_ = false;
    if (sock_ != ~uintptr_t{0})
    {
        closesocket(static_cast<SOCKET>(sock_));
        sock_ = ~uintptr_t{0};
    }
    if (wsa_)
    {
        WSACleanup();
        wsa_ = false;
    }
}

void UdpListener::Loop(std::stop_token st)
{
    const SOCKET s = static_cast<SOCKET>(sock_);
    std::vector<char> buf(65536);
    u_long nonblock = 1;
    ioctlsocket(s, FIONBIO, &nonblock); // select() says "readable", then we drain without blocking
    while (!st.stop_requested())
    {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(s, &rd);
        timeval tv{0, 100 * 1000}; // wake up 10x/s to notice stop requests
        if (select(0, &rd, nullptr, nullptr, &tv) <= 0) continue;

        // Drain the socket; if we fell behind only the newest valid snapshot is published.
        std::shared_ptr<WorldSnapshot> back;
        for (;;)
        {
            int n = recvfrom(s, buf.data(), static_cast<int>(buf.size()), 0, nullptr, nullptr);
            if (n <= 0) break;
            auto snap = std::make_shared<WorldSnapshot>();
            if (ParseDatagram(buf.data(), static_cast<size_t>(n), *snap)) back = std::move(snap);
            else ingest_.NoteRejected();
        }
        if (!back) continue;
        ingest_.Publish(std::move(back), Clock::now()); // seq + arrival time + pointer swap
    }
}
