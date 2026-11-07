#include "Clipboard.h"

#ifdef _WIN32
#include "Platform.h"
#include <thread>
#endif

namespace clipboard
{
    std::string ToCrlf(const std::string& s)
    {
        std::string o;
        o.reserve(s.size() + s.size() / 20);
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') continue; // already CRLF: the '\n' adds the pair
            if (s[i] == '\n') o += '\r';
            o += s[i];
        }
        return o;
    }

    std::string ToLf(const std::string& s)
    {
        std::string o;
        o.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') continue; // CRLF: the '\n' stays
            o += s[i];
        }
        return o;
    }

#ifdef _WIN32
    bool SetText(const std::string& utf8)
    {
        const std::string crlf = ToCrlf(utf8);
        const int n = MultiByteToWideChar(CP_UTF8, 0, crlf.data(), static_cast<int>(crlf.size()), nullptr, 0);
        if (n < 0 || (n == 0 && !crlf.empty())) return false;
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (static_cast<SIZE_T>(n) + 1) * sizeof(wchar_t));
        if (!mem) return false;
        wchar_t* w = static_cast<wchar_t*>(GlobalLock(mem));
        if (!w)
        {
            GlobalFree(mem);
            return false;
        }
        MultiByteToWideChar(CP_UTF8, 0, crlf.data(), static_cast<int>(crlf.size()), w, n);
        w[n] = 0;
        GlobalUnlock(mem);
        bool opened = false;
        for (int attempt = 0; attempt < 10 && !opened; ++attempt)
        {
            opened = OpenClipboard(nullptr) != 0;
            if (!opened) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (!opened)
        {
            GlobalFree(mem);
            return false;
        }
        EmptyClipboard();
        const bool ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr; // on success the clipboard owns `mem`
        if (!ok) GlobalFree(mem);
        CloseClipboard();
        return ok;
    }

    bool GetText(std::string& utf8, size_t maxBytes)
    {
        bool opened = false;
        for (int attempt = 0; attempt < 10 && !opened; ++attempt)
        {
            opened = OpenClipboard(nullptr) != 0;
            if (!opened) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (!opened) return false;
        bool ok = false;
        if (HANDLE h = GetClipboardData(CF_UNICODETEXT))
        {
            const SIZE_T bytes = GlobalSize(h);
            if (const wchar_t* w = static_cast<const wchar_t*>(GlobalLock(h)))
            {
                size_t n = 0;
                const size_t cap = bytes / sizeof(wchar_t);
                while (n < cap && w[n]) ++n; // up to the terminator, never past the block
                const int need = n == 0 ? 0 : WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(n), nullptr, 0, nullptr, nullptr);
                if (need > 0 && static_cast<size_t>(need) <= maxBytes)
                {
                    std::string s(static_cast<size_t>(need), '\0');
                    WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(n), s.data(), need, nullptr, nullptr);
                    utf8 = ToLf(s);
                    ok = true;
                }
                GlobalUnlock(h);
            }
        }
        CloseClipboard();
        return ok;
    }
#else
    bool SetText(const std::string&) { return false; }
    bool GetText(std::string&, size_t) { return false; }
#endif
} // namespace clipboard
