// Clipboard.h - put text on the Windows clipboard (CF_UNICODETEXT). Used by "Copy diagnostics".
#pragma once
#include <string>

namespace clipboard
{
    // `utf8` is converted to UTF-16 with CRLF line ends. Another program may hold the clipboard for a moment, so opening it is retried briefly.
    // Returns false if it could not be done (and always on non-Windows hosts, where there is nothing to copy to).
    bool SetText(const std::string& utf8);

    // The CRLF conversion alone, for tests (platform independent).
    std::string ToCrlf(const std::string& s);
} // namespace clipboard
