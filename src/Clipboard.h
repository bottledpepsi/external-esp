// Clipboard.h - put text on the Windows clipboard (CF_UNICODETEXT). Used by "Copy diagnostics".
#pragma once
#include <string>

namespace clipboard
{
    // `utf8` is converted to UTF-16 with CRLF line ends. Another program may hold the clipboard for a moment, so opening it is retried briefly.
    // Returns false if it could not be done (and always on non-Windows hosts, where there is nothing to copy to).
    bool SetText(const std::string& utf8);

    // The clipboard's text as UTF-8 with LF line ends, at most `maxBytes` (longer text is refused: false). False when there is no text, the clipboard
    // could not be opened, or on non-Windows hosts.
    bool GetText(std::string& utf8, size_t maxBytes = 64 * 1024);

    // The CRLF conversion alone, for tests (platform independent).
    std::string ToCrlf(const std::string& s);
    std::string ToLf(const std::string& s); // CRLF -> LF
} // namespace clipboard
