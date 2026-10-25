// Logger: line format, levels, one-line-per-message, size cap with one backup, rotation of an oversized old log, concurrent writers.
#include "Logger.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

namespace fs = std::filesystem;

static std::string Read(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static std::vector<std::string> Lines(const std::string& s)
{
    std::vector<std::string> v;
    std::istringstream is(s);
    for (std::string l; std::getline(is, l);) v.push_back(l);
    return v;
}

// HH:MM:SS.mmm [LEVEL] [esp] <text starting with a non-space>
static bool LineOk(const std::string& l)
{
    if (l.size() < 28) return false;
    auto digit = [&](size_t i) { return l[i] >= '0' && l[i] <= '9'; };
    for (size_t i : {0u, 1u, 3u, 4u, 6u, 7u, 9u, 10u, 11u}) if (!digit(i)) return false;
    if (l[2] != ':' || l[5] != ':' || l[8] != '.' || l[12] != ' ' || l[13] != '[' || l[19] != ']' || l[20] != ' ') return false;
    const std::string lvl = l.substr(14, 5);
    if (lvl != "DEBUG" && lvl != "INFO " && lvl != "WARN " && lvl != "ERROR") return false;
    return l.compare(21, 6, "[esp] ") == 0 && l[27] != ' ';
}

int main()
{
    // ---- format (pure function) ----
    {
        CHECK(elog::FormatLine(elog::Level::Info, "esp", "hello", 9, 5, 7, 42) == "09:05:07.042 [INFO ] [esp] hello");
        CHECK(elog::FormatLine(elog::Level::Warn, "agent", "x y", 23, 59, 59, 999) == "23:59:59.999 [WARN ] [agent] x y");
        CHECK(elog::FormatLine(elog::Level::Error, "esp", "e", 0, 0, 0, 0) == "00:00:00.000 [ERROR] [esp] e");
        CHECK(elog::FormatLine(elog::Level::Debug, "esp", "d", 0, 0, 0, 0) == "00:00:00.000 [DEBUG] [esp] d");
        // one message = one line: control characters cannot forge another one
        CHECK(elog::SanitizeMessage("a\nb\r\nc\td\x01" "e\x7F" "f") == "a b  c d e f");
        CHECK(elog::SanitizeMessage("tail   \n") == "tail");
        const std::string longMsg(10000, 'x');
        const std::string cut = elog::SanitizeMessage(longMsg);
        CHECK(cut.size() == elog::kMaxMessageChars && cut.substr(cut.size() - 3) == "...");
        CHECK(elog::SanitizeMessage(std::string(elog::kMaxMessageChars, 'y')).size() == elog::kMaxMessageChars);   // exactly the limit: untouched
    }

    const fs::path dir = fs::current_path() / "logtest";
    fs::remove_all(dir);
    fs::create_directories(dir);

    // ---- levels, file content, injection ----
    {
        elog::SetLogPath(dir / "a.txt");
        elog::SetMaxBytes(1 << 20);
        elog::SetLevel(elog::Level::Info);
        CHECK(elog::GetLevel() == elog::Level::Info && !elog::Enabled(elog::Level::Debug) && elog::Enabled(elog::Level::Info) && elog::Enabled(elog::Level::Error));
        LOG_DEBUG("debug is off by default");
        LOG_INFO("info %d", 1);
        LOG_WARN("warn %s", "two");
        LOG_ERROR("error");
        LOG_INFO("forged\n12:00:00.000 [ERROR] [esp] this is not a real line");
        elog::SetLevel(elog::Level::Debug);                                               // --verbose
        LOG_DEBUG("now %s", "visible");
        elog::SetLevel(elog::Level::Error);
        LOG_WARN("hidden: below the level");
        LOG_ERROR("still shown");
        elog::SetLevel(elog::Level::Info);
        elog::Close();
        const auto lines = Lines(Read(dir / "a.txt"));
        CHECK(lines.size() == 6);
        bool allMatch = true;
        for (const auto& l : lines) allMatch = allMatch && LineOk(l);
        CHECK(allMatch);
        CHECK(lines.size() == 6 && lines[0].find("[INFO ] [esp] info 1") != std::string::npos && lines[1].find("[WARN ] [esp] warn two") != std::string::npos
              && lines[2].find("[ERROR] [esp] error") != std::string::npos);
        CHECK(lines.size() == 6 && lines[3].find("forged 12:00:00.000 [ERROR] [esp] this is not a real line") != std::string::npos);   // ONE line, the fake header is just text
        CHECK(lines.size() == 6 && lines[4].find("[DEBUG] [esp] now visible") != std::string::npos && lines[5].find("still shown") != std::string::npos);
        CHECK(Read(dir / "a.txt").find("debug is off") == std::string::npos && Read(dir / "a.txt").find("hidden") == std::string::npos);
        // a long message is cut, still one line
        LOG_INFO("%s", std::string(20000, 'z').c_str());
        elog::Close();
        const auto l2 = Lines(Read(dir / "a.txt"));
        CHECK(l2.size() == 7 && l2.back().size() < 4100 && l2.back().find("...") != std::string::npos);
    }

    // ---- size cap: one backup, never more than about two files' worth ----
    {
        elog::SetLogPath(dir / "b.txt");
        elog::SetMaxBytes(4096);
        for (int i = 0; i < 2000; ++i) LOG_INFO("line number %04d with some padding to make it longer %s", i, "................");
        elog::Close();
        CHECK(fs::exists(dir / "b.txt") && fs::exists(dir / "b.1.txt") && elog::BackupFile() == dir / "b.1.txt" && elog::LogFile() == dir / "b.txt");
        const auto cur = fs::file_size(dir / "b.txt"), old = fs::file_size(dir / "b.1.txt");
        CHECK(cur < 4096 + 200 && old < 4096 + 200);                                      // each file stops at the cap (plus the line that crossed it)
        const auto curLines = Lines(Read(dir / "b.txt")), oldLines = Lines(Read(dir / "b.1.txt"));
        CHECK(!curLines.empty() && !oldLines.empty());
        CHECK(curLines.back().find("line number 1999") != std::string::npos);            // the newest line is in the current file
        // the two files are consecutive: the backup ends right before the current file begins
        const auto firstCur = curLines.front(), lastOld = oldLines.back();
        const int a = std::stoi(lastOld.substr(lastOld.find("number ") + 7, 4)), b = std::stoi(firstCur.substr(firstCur.find("number ") + 7, 4));
        CHECK(b == a + 1);
        bool allMatch = true;
        for (const auto& l : curLines) allMatch = allMatch && LineOk(l);
        for (const auto& l : oldLines) allMatch = allMatch && LineOk(l);
        CHECK(allMatch);                                                                  // rotation never cuts a line in half
        std::printf("rotation: 2000 lines -> %s %llu B + %s %llu B\n", "b.txt", static_cast<unsigned long long>(cur), "b.1.txt", static_cast<unsigned long long>(old));
    }

    // ---- an oversized log left by the previous run is rotated at start-up ----
    {
        fs::remove(dir / "c.txt");
        fs::remove(dir / "c.1.txt");
        { std::ofstream f(dir / "c.txt", std::ios::binary); f << std::string(50000, 'o') << "\n"; }
        elog::SetLogPath(dir / "c.txt");
        elog::SetMaxBytes(4096);
        elog::Init();
        elog::Close();
        CHECK(fs::exists(dir / "c.1.txt") && fs::file_size(dir / "c.1.txt") > 50000);
        const auto lines = Lines(Read(dir / "c.txt"));
        CHECK(lines.size() == 3 && lines[1].find("session started") != std::string::npos);   // a fresh file with the banner
        // a small old log is appended to, not rotated
        elog::SetLogPath(dir / "d.txt");
        elog::SetMaxBytes(1 << 20);
        LOG_INFO("first run");
        elog::Close();
        elog::Init();
        elog::Close();
        CHECK(!fs::exists(dir / "d.1.txt") && Lines(Read(dir / "d.txt")).size() == 4);
    }

    // ---- four threads writing at once: every line is whole, none is lost, none is mixed ----
    {
        elog::SetLogPath(dir / "e.txt");
        elog::SetMaxBytes(64 << 20);
        std::vector<std::thread> th;
        for (int t = 0; t < 4; ++t)
            th.emplace_back([t] { for (int i = 0; i < 1500; ++i) LOG_INFO("thread %d message %04d %s", t, i, "payload payload payload"); });
        for (auto& x : th) x.join();
        elog::Close();
        const auto lines = Lines(Read(dir / "e.txt"));
        CHECK(lines.size() == 6000);
        bool ok = true;
        int seen[4] = {0, 0, 0, 0}, nextExpected[4] = {0, 0, 0, 0};
        for (const auto& l : lines)
        {
            int t = -1, i = -1;
            char tail[64] = {};
            const size_t at = l.find("[esp] thread ");
            if (at == std::string::npos || std::sscanf(l.c_str() + at, "[esp] thread %d message %d %63[a-z ]", &t, &i, tail) != 3 || std::string(tail) != "payload payload payload" || t < 0 || t > 3) { ok = false; continue; }
            ok = ok && i == nextExpected[t]++;                                           // per thread the order is preserved
            ++seen[t];
        }
        CHECK(ok && seen[0] == 1500 && seen[1] == 1500 && seen[2] == 1500 && seen[3] == 1500);
    }

    // ---- an unwritable location does not crash or block ----
    {
        elog::SetLogPath(dir / "no" / "such" / "dir" / "f.txt");
        LOG_INFO("goes to the console only");
        LOG_ERROR("still no crash");
        elog::Close();
        CHECK(true);
    }

    elog::SetLogPath(dir / "z.txt");
    std::printf(fails ? "\n%d FAILURES\n" : "\nALL OK\n", fails);
    return fails;
}
