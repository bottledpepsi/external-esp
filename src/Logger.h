// Logger.h - esp_log.txt next to the exe, mirrored to the console (this build is a console-subsystem exe: no -mwindows).
//
// One line format shared with the agent's esp_agent.log (agent/src/mcradar/Log.java):
//     HH:MM:SS.mmm [LEVEL] [source] message          LEVEL = ERROR | WARN  | INFO  | DEBUG (padded to 5), source = esp | agent
// Default level is INFO; --verbose (SetLevel(Debug)) adds DEBUG. A message is always ONE line: newlines and other control characters in it become
// spaces, so nothing the game or the network sends can forge a log line. The file is capped (1 MiB by default): when it is full it is renamed to
// esp_log.1.txt (replacing the previous backup) and a new one is started, so at most about 2 MiB are ever used, however long the overlay runs.
#pragma once
#include <cstdarg>
#include <cstddef>
#include <filesystem>
#include <string>

#if defined(__GNUC__)
#define ESP_PRINTF(f, a) __attribute__((format(printf, f, a)))
#else
#define ESP_PRINTF(f, a)
#endif

namespace elog
{
    enum class Level { Debug = 0, Info = 1, Warn = 2, Error = 3 };

    inline constexpr size_t kDefaultMaxBytes = 1u << 20; // per file; one backup is kept
    inline constexpr size_t kMaxMessageChars = 4000; // longer messages are cut and end in "..."

    void Init(); // rotate an oversized old log, write the session banner
    ESP_PRINTF(2, 3) void Write(Level lvl, const char* fmt, ...);

    void SetLevel(Level minimum); // messages below it are dropped (default Info)
    Level GetLevel();
    bool Enabled(Level lvl); // so expensive DEBUG arguments can be skipped

    // Where the log goes and how big it may get. The defaults are AppDir()/esp_log.txt and kDefaultMaxBytes; tests and tools override them.
    void SetLogPath(const std::filesystem::path& file);
    void SetMaxBytes(size_t bytes);
    std::filesystem::path LogFile();
    std::filesystem::path BackupFile(); // esp_log.1.txt next to the log
    void Close(); // flush and release the file (it is reopened by the next Write)

    // Formats one line exactly like Write does, without writing it (tests, diagnostics).
    std::string FormatLine(Level lvl, const char* source, const std::string& message, int hour, int minute, int second, int millis);
    std::string SanitizeMessage(const std::string& message); // control characters -> ' ', cut at kMaxMessageChars
} // namespace elog

#define LOG_DEBUG(...) ::elog::Write(::elog::Level::Debug, __VA_ARGS__)
#define LOG_INFO(...)  ::elog::Write(::elog::Level::Info, __VA_ARGS__)
#define LOG_WARN(...)  ::elog::Write(::elog::Level::Warn, __VA_ARGS__)
#define LOG_ERROR(...) ::elog::Write(::elog::Level::Error, __VA_ARGS__)
