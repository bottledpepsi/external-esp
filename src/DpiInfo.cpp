#include "DpiInfo.h"
#include "DpiFormat.h"
#include <vector>

namespace dpi
{
    namespace
    {
        // Not in every SDK / older mingw headers: values and signatures from the Windows documentation.
        using SetCtxFn = BOOL(WINAPI*)(HANDLE);
        using SetAwarenessFn = HRESULT(WINAPI*)(int);
        using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
        using GetWindowCtxFn = HANDLE(WINAPI*)(HWND);
        using GetAwarenessFromCtxFn = int(WINAPI*)(HANDLE);

        const HANDLE kPerMonitorV2 = reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        constexpr int kPerMonitorAware = 2; // PROCESS_PER_MONITOR_DPI_AWARE
        constexpr int kMdtEffectiveDpi = 0; // MDT_EFFECTIVE_DPI

        std::string g_level;

        struct MonCtx
        {
            std::vector<std::string>* lines;
            GetDpiForMonitorFn getDpi;
            int index = 0;
        };

        BOOL CALLBACK MonProc(HMONITOR m, HDC, LPRECT, LPARAM lp)
        {
            auto* c = reinterpret_cast<MonCtx*>(lp);
            MONITORINFO mi{};
            mi.cbSize = sizeof mi;
            if (!GetMonitorInfoW(m, &mi)) return TRUE;
            UINT dx = 0, dy = 0;
            if (c->getDpi && FAILED(c->getDpi(m, kMdtEffectiveDpi, &dx, &dy))) dx = 0;
            c->lines->push_back(MonitorLine(++c->index, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, mi.rcMonitor.left, mi.rcMonitor.top, dx,
                                           (mi.dwFlags & MONITORINFOF_PRIMARY) != 0));
            return TRUE;
        }
    } // namespace

    std::string Enable()
    {
        if (HMODULE u = GetModuleHandleW(L"user32.dll"))
        {
            if (auto f = reinterpret_cast<SetCtxFn>(reinterpret_cast<void*>(GetProcAddress(u, "SetProcessDpiAwarenessContext"))))
                if (f(kPerMonitorV2)) return g_level = "per-monitor v2 (SetProcessDpiAwarenessContext)";
        }
        if (HMODULE sh = LoadLibraryW(L"shcore.dll"))
        {
            if (auto f = reinterpret_cast<SetAwarenessFn>(reinterpret_cast<void*>(GetProcAddress(sh, "SetProcessDpiAwareness"))))
            {
                const HRESULT hr = f(kPerMonitorAware);
                if (SUCCEEDED(hr)) return g_level = "per-monitor (SetProcessDpiAwareness)";
            }
        }
        if (SetProcessDPIAware()) return g_level = "system aware only (SetProcessDPIAware): an overlay over a game on a monitor with a different scale can be offset";
        return g_level = "DPI awareness could not be set (already set by a manifest, or unavailable)";
    }

    std::string DescribeMonitors()
    {
        GetDpiForMonitorFn getDpi = nullptr;
        if (HMODULE sh = LoadLibraryW(L"shcore.dll")) getDpi = reinterpret_cast<GetDpiForMonitorFn>(reinterpret_cast<void*>(GetProcAddress(sh, "GetDpiForMonitor")));
        std::vector<std::string> lines;
        MonCtx c{&lines, getDpi};
        EnumDisplayMonitors(nullptr, nullptr, MonProc, reinterpret_cast<LPARAM>(&c));
        std::string out;
        for (const auto& l : lines) out += (out.empty() ? "" : "; ") + l;
        return out.empty() ? "no monitor information" : out;
    }

    std::string WindowAwareness(HWND hwnd)
    {
        HMODULE u = GetModuleHandleW(L"user32.dll");
        if (!u || !hwnd) return "unknown";
        auto getCtx = reinterpret_cast<GetWindowCtxFn>(reinterpret_cast<void*>(GetProcAddress(u, "GetWindowDpiAwarenessContext")));
        auto toAw = reinterpret_cast<GetAwarenessFromCtxFn>(reinterpret_cast<void*>(GetProcAddress(u, "GetAwarenessFromDpiAwarenessContext")));
        if (!getCtx || !toAw) return "unknown (Windows older than 10 1607)";
        const int a = toAw(getCtx(hwnd)); // 0 unaware, 1 system, 2 per-monitor
        return a == 0 ? "unaware" : a == 1 ? "system" : a == 2 ? "per-monitor" : "unknown";
    }
} // namespace dpi
