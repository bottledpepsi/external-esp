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
#else
    bool SetText(const std::string&) { return false; }
#endif
} // namespace clipboard
