// SingleInstance.h - one overlay per UDP port. A second `mcesp` on the same port could not receive anything anyway (the port is taken); this says so
// at once, naming the running one, instead of failing later in the log. Different ports (two clients on one PC) are independent.
#pragma once
#include <cstdint>
#include <string>
#ifdef _WIN32
#include "Platform.h"
#endif

class SingleInstance
{
public:
    SingleInstance() = default;
    ~SingleInstance();
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    // The name of the per-session mutex for a port; the same port always gives the same name, different ports different names. (Portable.)
    static std::string MutexName(uint16_t port);
    // The title of the overlay window for a port, used to find the running instance.
    static std::wstring WindowTitle(uint16_t port);

#ifdef _WIN32
    // True when this process is the first one on `port` (and keeps the claim until it exits). False: another one holds it; `OtherPid()` is its
    // process id when its window could be found (0 when it has not created it yet).
    bool Acquire(uint16_t port);
    DWORD OtherPid() const { return other_; }
    static constexpr const wchar_t* kWindowClass = L"ExternalEspOverlay";

private:
    HANDLE mutex_ = nullptr;
    DWORD other_ = 0;
#endif
};
