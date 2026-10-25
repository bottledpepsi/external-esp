#include "Token.h"
#include <cstdint>
#include <cstdio>

#ifdef _WIN32
#include "Platform.h"
#include <bcrypt.h>
#endif

namespace token
{
    namespace
    {
        bool RandomBytes(unsigned char* out, size_t n)
        {
#ifdef _WIN32
            return BCryptGenRandom(nullptr, out, static_cast<ULONG>(n), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
            std::FILE* f = std::fopen("/dev/urandom", "rb");
            if (!f) return false;
            const size_t got = std::fread(out, 1, n, f);
            std::fclose(f);
            return got == n;
#endif
        }
    } // namespace

    std::string Generate()
    {
        unsigned char b[kBytes];
        if (!RandomBytes(b, sizeof b)) return {};
        static const char hex[] = "0123456789abcdef";
        std::string s;
        for (unsigned char c : b)
        {
            s += hex[c >> 4];
            s += hex[c & 15];
        }
        return s;
    }

    bool IsValid(const std::string& t)
    {
        if (t.size() < 16 || t.size() > 64) return false;
        for (char c : t)
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
        return true;
    }

    bool Equals(const std::string& a, const std::string& b)
    {
        unsigned diff = static_cast<unsigned>(a.size() ^ b.size());
        const size_t n = a.size() < b.size() ? a.size() : b.size();
        for (size_t i = 0; i < n; ++i) diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
        return diff == 0;
    }
} // namespace token
