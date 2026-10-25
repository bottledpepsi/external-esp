#include "SysInfo.h"
#include <cstdlib>
#include <thread>

#ifdef _WIN32
#include "Platform.h"
#else
#include <sys/utsname.h>
#include <unistd.h>
#endif

namespace sysinfo
{
    std::string CompilerName()
    {
#if defined(__clang__)
        return "Clang " + std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__) + "." + std::to_string(__clang_patchlevel__);
#elif defined(__GNUC__)
        return "GCC " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__) + "." + std::to_string(__GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
        return "MSVC " + std::to_string(_MSC_VER);
#else
        return "unknown compiler";
#endif
    }

#ifdef _WIN32
    namespace
    {
        std::string Narrow(const wchar_t* w, int n)
        {
            if (n <= 0) return {};
            const int len = WideCharToMultiByte(CP_UTF8, 0, w, n, nullptr, 0, nullptr, nullptr);
            std::string s(static_cast<size_t>(len), '\0');
            WideCharToMultiByte(CP_UTF8, 0, w, n, s.data(), len, nullptr, nullptr);
            return s;
        }
    } // namespace

    Info Collect()
    {
        Info i;
        // RtlGetVersion tells the truth; GetVersionEx lies to programs without a manifest
        using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
        OSVERSIONINFOW vi{};
        vi.dwOSVersionInfoSize = sizeof vi;
        if (HMODULE nt = GetModuleHandleW(L"ntdll.dll"))
            if (auto fn = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(nt, "RtlGetVersion")))) fn(&vi);
        i.os = "Windows " + std::to_string(vi.dwMajorVersion) + "." + std::to_string(vi.dwMinorVersion) + " build " + std::to_string(vi.dwBuildNumber);
        i.cpu = std::to_string(std::thread::hardware_concurrency()) + " logical processors";
        wchar_t buf[256];
        DWORD n = 256;
        if (GetUserNameW(buf, &n) && n > 1) i.userName = Narrow(buf, static_cast<int>(n) - 1);
        n = 256;
        if (GetComputerNameW(buf, &n)) i.hostName = Narrow(buf, static_cast<int>(n));
        const DWORD pn = GetEnvironmentVariableW(L"USERPROFILE", buf, 256);
        if (pn > 0 && pn < 256) i.userProfile = Narrow(buf, static_cast<int>(pn));
        return i;
    }
#else
    Info Collect()
    {
        Info i;
        utsname u{};
        if (uname(&u) == 0) i.os = std::string(u.sysname) + " " + u.release;
        i.cpu = std::to_string(std::thread::hardware_concurrency()) + " logical processors";
        if (const char* v = std::getenv("USER")) i.userName = v;
        char h[256] = {};
        if (gethostname(h, sizeof h - 1) == 0) i.hostName = h;
        if (const char* v = std::getenv("HOME")) i.userProfile = v;
        return i;
    }
#endif
} // namespace sysinfo
