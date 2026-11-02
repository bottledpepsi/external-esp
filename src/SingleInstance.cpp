#include "SingleInstance.h"

std::string SingleInstance::MutexName(uint16_t port) { return "Local\\external-esp-overlay-" + std::to_string(port); }
std::wstring SingleInstance::WindowTitle(uint16_t port) { return L"external-esp :" + std::to_wstring(port); }

#ifdef _WIN32
SingleInstance::~SingleInstance()
{
    if (mutex_) CloseHandle(mutex_); // the claim ends with the process anyway
}

bool SingleInstance::Acquire(uint16_t port)
{
    const std::string name = MutexName(port);
    mutex_ = CreateMutexA(nullptr, FALSE, name.c_str());
    if (mutex_ && GetLastError() != ERROR_ALREADY_EXISTS) return true;
    if (!mutex_ && GetLastError() != ERROR_ACCESS_DENIED) return true; // cannot tell (no mutex support): do not block the user
    // somebody else has it: find out who, for the message
    HWND w = FindWindowW(kWindowClass, WindowTitle(port).c_str());
    if (w) GetWindowThreadProcessId(w, &other_);
    return false;
}
#else
SingleInstance::~SingleInstance() = default;
#endif
