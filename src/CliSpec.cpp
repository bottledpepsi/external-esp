#include "CliSpec.h"
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace cli
{
    namespace
    {
        constexpr Arg N = Arg::None, R = Arg::Required, O = Arg::Optional;
        constexpr Kind KA = Kind::Any, KI = Kind::Int, KW = Kind::Word;

        const std::vector<Flag> kFlags = {
            // name, alias, arg, kind, meta, min, max, group, help
            {"--demo", "", N, KA, "", 0, 0, "Start", "synthetic players (test the overlay and the settings window without Minecraft)"},
            {"--pid", "", R, KI, "N", 1, 4294967295LL, "Start", "attach to this java(w).exe process id (two Minecraft clients on one PC; see docs/launchers.md)"},
            {"--title", "", R, KA, "TEXT", 0, 0, "Start", "pick the Minecraft client whose window title contains TEXT (several clients, or a custom client)"},
            {"--exe", "", R, KA, "NAME", 0, 0, "Start", "... whose program name contains NAME (javaw.exe, java.exe); combines with --title"},
            {"--profile", "", R, KA, "NAME", 0, 0, "Start", "start with the saved profile NAME (it becomes the current settings; see the PROFILES tab)"},
            {"--open-config", "", N, KA, "", 0, 0, "Start", "open the settings window right away"},
            {"--list", "", N, KA, "", 0, 0, "Start", "list the running Minecraft/java processes and exit"},
            {"--help", "-h", N, KA, "", 0, 0, "Start", "print this text and exit"},

            {"--port", "", R, KI, "N", 1, 65535, "Agent", "UDP port for the agent stream (default 47615); one overlay per port"},
            {"--hz", "", R, KI, "N", 1, 240, "Agent", "agent snapshot rate (default 120, or the AgentHz setting); more = smoother, a little more CPU in the game"},
            {"--no-inject", "", N, KA, "", 0, 0, "Agent", "do not attach automatically (the exact -javaagent line, with the session token, is printed)"},
            {"--java", "", R, KA, "PATH", 0, 0, "Agent", "java.exe of a JDK for the attach helper (default: the game's JRE, JAVA_HOME, PATH)"},
            {"--jar", "", R, KA, "PATH", 0, 0, "Agent", "agent jar to use instead of the one inside the exe"},
            {"--agent-pacer", "", R, KW, "sleep|hybrid", 0, 0, "Agent", "how the agent waits for its next tick: sleep (default) or hybrid (spins the last ~2 ms; precise, costs CPU in the game)"},
            {"--token", "", R, KA, "T", 0, 0, "Agent", "use this session token instead of a random one (16-64 letters/digits; with --no-inject pass the same to the agent)"},
            {"--no-auth", "", N, KA, "", 0, 0, "Agent", "accept datagrams without / with any session token (not recommended)"},

            {"--screen", "", N, KA, "", 0, 0, "Display", "cover the whole primary monitor instead of tracking the game window"},
            {"--gdi", "", N, KA, "", 0, 0, "Display", "use the legacy CPU renderer (layered window) instead of the GPU swap chain"},
            {"--adapter", "", R, KA, "X", 0, 0, "Display", "GPU for the overlay: its number or part of its name (default: automatic; the log lists every adapter)"},

            {"--record", "", R, KA, "FILE", 0, 0, "Recording, tests and diagnostics", "also save the raw agent datagrams with arrival times to FILE (.esprec)"},
            {"--replay", "", R, KA, "FILE", 0, 0, "Recording, tests and diagnostics", "play a recording over the whole screen instead of attaching (F6 pause, F7 step, F8 slower, F9 faster)"},
            {"--replay-speed", "", R, KA, "X", 0, 0, "Recording, tests and diagnostics", "playback speed for --replay (1 = real time)"},
            {"--replay-loop", "", N, KA, "", 0, 0, "Recording, tests and diagnostics", "repeat the recording"},
            {"--selftest", "", O, KI, "S", 1, 600, "Recording, tests and diagnostics", "attach, watch S seconds (default 20: look around, drop an item), print what works and exit with a code (docs/selftest.md)"},
            {"--pacing-report", "", O, KI, "S", 1, 3600, "Recording, tests and diagnostics", "the agent writes esp_pacing.txt after S seconds (default 20); docs/pacing.md"},
            {"--pacing-test", "", O, KI, "S", 1, 60, "Recording, tests and diagnostics", "measure Sleep(1), the high-resolution timer and the overlay's frame pacing (S s per case, default 2)"},
            {"--dump-structure", "", N, KA, "", 0, 0, "Recording, tests and diagnostics", "the agent also writes esp_structure.json (class structure only, for bug reports; docs/fixtures.md)"},
            {"--simulate-device-loss", "", N, KA, "", 0, 0, "Recording, tests and diagnostics", "test aid: F10 injects a GPU device loss"},
            {"--simulate-device-loss=persistent", "", N, KA, "", 0, 0, "Recording, tests and diagnostics", "... and recovery fails too, so the GDI fallback must follow"},
            {"--diagnostics", "", O, KA, "FILE", 0, 0, "Recording, tests and diagnostics", "print (or write to FILE) a REDACTED report for bug reports, then exit"},
            {"--verbose", "-v", N, KA, "", 0, 0, "Recording, tests and diagnostics", "also log DEBUG lines (esp_log.txt, capped at 1 MiB + one backup)"},
        };

        int Edit(const std::string& a, const std::string& b)
        {
            std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
            for (size_t j = 0; j <= b.size(); ++j) prev[j] = static_cast<int>(j);
            for (size_t i = 1; i <= a.size(); ++i)
            {
                cur[0] = static_cast<int>(i);
                for (size_t j = 1; j <= b.size(); ++j)
                    cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
                std::swap(prev, cur);
            }
            return prev[b.size()];
        }

        bool WholeNumber(const char* s, long long& out)
        {
            if (!s || !*s) return false;
            char* end = nullptr;
            errno = 0;
            const long long v = std::strtoll(s, &end, 10);
            if (*end != 0 || errno == ERANGE) return false;
            out = v;
            return true;
        }

        std::string Words(const std::string& pipeSeparated)
        {
            std::string out;
            std::stringstream ss(pipeSeparated);
            std::string w;
            std::vector<std::string> v;
            while (std::getline(ss, w, '|')) v.push_back(w);
            for (size_t i = 0; i < v.size(); ++i) out += (i ? (i + 1 == v.size() ? " or " : ", ") : "") + v[i];
            return out;
        }

        // the check of one value against its flag; "" when fine
        std::string BadValue(const Flag& f, const char* value)
        {
            if (f.kind == Kind::Int)
            {
                long long v = 0;
                if (!WholeNumber(value, v) || v < f.min || v > f.max)
                    return std::string(f.name) + ": '" + value + "' is not a whole number from " + std::to_string(f.min) + " to " + std::to_string(f.max);
            }
            else if (f.kind == Kind::Word)
            {
                std::stringstream ss(f.meta);
                std::string w;
                while (std::getline(ss, w, '|')) if (w == value) return {};
                return std::string(f.name) + ": '" + value + "' is not " + Words(f.meta);
            }
            return {};
        }
    } // namespace

    const std::vector<Flag>& Flags() { return kFlags; }

    const Flag* Find(const std::string& name)
    {
        for (const Flag& f : kFlags)
            if (name == f.name || (*f.alias && name == f.alias)) return &f;
        return nullptr;
    }

    std::string Suggest(const std::string& unknown)
    {
        std::string best;
        int bestScore = 3;
        for (const Flag& f : kFlags)
        {
            const std::string n = f.name;
            int d = Edit(unknown, n);
            if (n.rfind(unknown, 0) == 0 && unknown.size() >= 4) d = std::min(d, 1); // "--rep" -> a prefix of a real option
            if (d < bestScore) { bestScore = d; best = n; }
        }
        return best;
    }

    Check Preflight(int argc, const char* const* argv)
    {
        Check c;
        auto fail = [&](const std::string& msg)
        {
            c.result = Check::Result::Error;
            c.message = msg;
            return c;
        };
        for (int i = 1; i < argc; ++i)
        {
            const std::string a = argv[i];
            const Flag* f = Find(a);
            if (!f)
            {
                if (a.empty() || a[0] != '-') return fail("Unexpected argument '" + a + "'. Options start with '--'.");
                const std::string s = Suggest(a);
                return fail("Unknown option '" + a + "'." + (s.empty() ? std::string() : " Did you mean '" + s + "'?"));
            }
            if (f->arg == Arg::None)
            {
                if (std::string(f->name) == "--help") c.result = Check::Result::Help;
                continue;
            }
            const bool haveNext = i + 1 < argc;
            const bool nextIsOption = haveNext && argv[i + 1][0] == '-' && Find(argv[i + 1]);
            if (f->arg == Arg::Required)
            {
                if (!haveNext || nextIsOption) return fail(std::string(f->name) + " needs a value (" + f->meta + ").");
                if (const std::string bad = BadValue(*f, argv[i + 1]); !bad.empty()) return fail(bad + ".");
                ++i;
            }
            else if (haveNext && !nextIsOption && argv[i + 1][0] != '-')
            {
                if (f->kind == Kind::Int)
                    if (const std::string bad = BadValue(*f, argv[i + 1]); !bad.empty()) return fail(bad + ".");
                ++i;
            }
        }
        return c;
    }

    std::string Help()
    {
        std::string out =
            "external-esp (C++) - an ESP overlay for Minecraft that reads the game through a Java agent\n"
            "\n"
            "usage: mcesp [options]\n";
        std::string group;
        for (const Flag& f : kFlags)
        {
            if (group != f.group)
            {
                group = f.group;
                out += "\n" + group + ":\n";
            }
            std::string left = std::string("  ") + f.name;
            if (*f.alias) left += std::string(", ") + f.alias;
            if (f.arg == Arg::Required) left += std::string(" ") + f.meta;
            else if (f.arg == Arg::Optional) left += std::string(" [") + f.meta + "]";
            if (left.size() < 26) left.append(26 - left.size(), ' ');
            else left += "  ";
            // wrap the help text at ~100 columns, continuation lines indented under it
            std::string line = left;
            const size_t indent = std::max<size_t>(left.size(), 28);
            std::stringstream words(f.help);
            std::string w;
            bool first = true;
            while (words >> w)
            {
                if (!first && line.size() + 1 + w.size() > 100)
                {
                    out += line + "\n";
                    line.assign(indent, ' ');
                    first = true;
                }
                line += (first ? "" : " ") + w;
                first = false;
            }
            out += line + "\n";
        }
        out +=
            "\nKeys (default; change them in the HOTKEYS tab): Insert = settings window, Delete = show/hide, hold End = exit.\n"
            "Settings and profiles live in %APPDATA%\\external-esp (docs/config.md). Exit codes: docs/cli.md.\n";
        return out;
    }

    std::string DescribePortError(unsigned port, int wsaError)
    {
        const std::string p = std::to_string(port);
        if (wsaError == 10048) // WSAEADDRINUSE
            return "UDP port " + p + " is already in use by another program (not necessarily mcesp).\n"
                   "  Close that program, or choose another port:  mcesp --port <number>   (the agent is told the same port automatically).";
        if (wsaError == 10013) // WSAEACCES
            return "Windows does not allow UDP port " + p + " here: it is probably inside a range Windows has reserved (Hyper-V / WSL / Docker do this).\n"
                   "  See the ranges with:  netsh int ipv4 show excludedportrange protocol=udp\n"
                   "  and choose a port outside them:  mcesp --port <number>";
        return "Cannot listen on UDP port " + p + " (Windows error " + std::to_string(wsaError) + ").\n  Choose another port:  mcesp --port <number>";
    }
} // namespace cli
