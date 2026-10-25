// Clipboard: the CRLF conversion everywhere; on Windows also a real round trip through CF_UNICODETEXT (skipped, not failed, when the session has no clipboard).
#include "Clipboard.h"
#include <cstdio>
#include <string>

#ifdef _WIN32
#include "Platform.h"
#endif

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

int main()
{
    CHECK(clipboard::ToCrlf("a\nb\n") == "a\r\nb\r\n");
    CHECK(clipboard::ToCrlf("a\r\nb\r\n") == "a\r\nb\r\n");              // already CRLF: not doubled
    CHECK(clipboard::ToCrlf("a\rb") == "a\rb" && clipboard::ToCrlf("") == "" && clipboard::ToCrlf("\n") == "\r\n" && clipboard::ToCrlf("\n\n") == "\r\n\r\n");
    CHECK(clipboard::ToCrlf("x\r\n\ny") == "x\r\n\r\ny");

#ifdef _WIN32
    const std::string text = "diagnostics test\nline 2 with J\xC3\xBCrgen and \xE2\x82\xAC\n";
    if (!clipboard::SetText(text)) std::printf("SKIP: this session has no usable clipboard\n");
    else
    {
        CHECK(OpenClipboard(nullptr) != 0);
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        CHECK(h != nullptr);
        if (h)
        {
            const wchar_t* w = static_cast<const wchar_t*>(GlobalLock(h));
            CHECK(w != nullptr);
            if (w)
            {
                const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
                std::string back(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
                WideCharToMultiByte(CP_UTF8, 0, w, -1, back.data(), n, nullptr, nullptr);
                CHECK(back == clipboard::ToCrlf(text));                           // the text, UTF-8 intact, CRLF line ends
                GlobalUnlock(h);
            }
        }
        CloseClipboard();
        std::printf("clipboard round trip done\n");
    }
    CHECK(clipboard::SetText("") || true);                                     // an empty report must not crash
#else
    CHECK(!clipboard::SetText("x"));                                           // nothing to copy to on this host
#endif

    std::printf(fails ? "\n%d FAILURES\n" : "\nALL OK\n", fails);
    return fails;
}
