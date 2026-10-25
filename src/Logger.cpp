#include "Logger.h"
#include "Paths.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>
#include <system_error>
#include <vector>

namespace elog
{
    namespace
    {
        std::mutex g_mtx;
        std::filesystem::path g_path; // empty = AppDir()/esp_log.txt, resolved on first use
        size_t g_maxBytes = kDefaultMaxBytes;
        std::ofstream g_file;
        uint64_t g_size = 0; // bytes in the current file
        std::atomic<int> g_level{static_cast<int>(Level::Info)};

        std::filesystem::path PathLocked()
        {
            if (g_path.empty()) g_path = AppDir() / "esp_log.txt";
            return g_path;
        }

        std::filesystem::path BackupOf(const std::filesystem::path& p)
        {
            return p.parent_path() / (p.stem().string() + ".1" + p.extension().string()); // esp_log.txt -> esp_log.1.txt
        }

        // Called with g_mtx held: move a full log aside (replacing the old backup).
        void RotateLocked()
        {
            g_file.close();
            std::error_code ec;
            const auto p = PathLocked();
            std::filesystem::remove(BackupOf(p), ec);
            std::filesystem::rename(p, BackupOf(p), ec);
            if (ec) std::filesystem::remove(p, ec); // cannot rename (file locked?): at least do not grow without bound
            g_size = 0;
        }

        bool OpenLocked()
        {
            if (g_file.is_open()) return true;
            std::error_code ec;
            const auto p = PathLocked();
            const auto sz = std::filesystem::file_size(p, ec);
            g_size = ec ? 0 : static_cast<uint64_t>(sz);
            if (g_size >= g_maxBytes) RotateLocked();
            g_file.open(p, std::ios::app);
            return g_file.is_open();
        }

        const char* Tag(Level l)
        {
            switch (l)
            {
            case Level::Debug: return "DEBUG";
            case Level::Info: return "INFO ";
            case Level::Warn: return "WARN ";
            case Level::Error: return "ERROR";
            }
            return "?    ";
        }
    } // namespace

    std::string SanitizeMessage(const std::string& m)
    {
        std::string o = m.size() > kMaxMessageChars ? m.substr(0, kMaxMessageChars - 3) + "..." : m;
        for (char& c : o)
            if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) c = ' ';
        while (!o.empty() && o.back() == ' ') o.pop_back();
        return o;
    }

    std::string FormatLine(Level lvl, const char* source, const std::string& message, int h, int mi, int s, int ms)
    {
        char head[48];
        std::snprintf(head, sizeof head, "%02d:%02d:%02d.%03d [%s] [%s] ", h, mi, s, ms, Tag(lvl), source);
        return std::string(head) + SanitizeMessage(message);
    }

    void SetLevel(Level l) { g_level = static_cast<int>(l); }
    Level GetLevel() { return static_cast<Level>(g_level.load()); }
    bool Enabled(Level l) { return static_cast<int>(l) >= g_level.load(); }

    void SetLogPath(const std::filesystem::path& file)
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        g_file.close();
        g_path = file;
    }

    void SetMaxBytes(size_t bytes)
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        g_maxBytes = bytes < 256 ? 256 : bytes;
    }

    std::filesystem::path LogFile()
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        return PathLocked();
    }

    std::filesystem::path BackupFile()
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        return BackupOf(PathLocked());
    }

    void Close()
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        g_file.close();
    }

    void Write(Level lvl, const char* fmt, ...)
    {
        if (!Enabled(lvl)) return;
        // Format into a buffer that is big enough (a vsnprintf size probe), then let SanitizeMessage cut it.
        va_list ap, ap2;
        va_start(ap, fmt);
        va_copy(ap2, ap);
        const int need = std::vsnprintf(nullptr, 0, fmt, ap);
        va_end(ap);
        std::string msg;
        if (need > 0)
        {
            std::vector<char> buf(static_cast<size_t>(need) + 1);
            std::vsnprintf(buf.data(), buf.size(), fmt, ap2);
            msg.assign(buf.data(), std::min<size_t>(static_cast<size_t>(need), kMaxMessageChars * 2));
        }
        va_end(ap2);

        const auto now = std::chrono::system_clock::now();
        const std::time_t t = std::chrono::system_clock::to_time_t(now);
        const int ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);

        std::lock_guard<std::mutex> lock(g_mtx);
        const std::tm tmv = *std::localtime(&t); // guarded by g_mtx
        const std::string line = FormatLine(lvl, "esp", msg, tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ms);

        std::fprintf(stdout, "%s\n", line.c_str());
        std::fflush(stdout);
        try
        {
            if (!OpenLocked()) return;
            g_file << line << '\n';
            g_file.flush(); // a crash must not eat the last lines
            g_size += line.size() + 1;
            if (g_size >= g_maxBytes) RotateLocked(); // the next Write reopens a fresh file
        }
        catch (...)
        {
            /* nothing useful to do if logging itself fails */
        }
    }

    void Init()
    {
        {
            std::lock_guard<std::mutex> lock(g_mtx);
            try { OpenLocked(); } // rotates an oversized log left by the last run
            catch (...) { }
        }
        Write(Level::Info, "===========================================");
        const std::time_t t = std::time(nullptr);
        char buf[32];
        {
            std::lock_guard<std::mutex> lock(g_mtx);
            std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", std::localtime(&t));
        }
        Write(Level::Info, "external-esp (C++)  session started  %s", buf);
        Write(Level::Info, "===========================================");
    }
} // namespace elog
