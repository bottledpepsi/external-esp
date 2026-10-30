// ProcessSelect.h - which java process is "the" Minecraft client, and what to tell the user when that is unclear or the attach fails.
// Portable (no Windows headers): tests/procselect_test.cpp runs it on any platform. ProcessManager and --selftest both use Select().
//
// Tiers, first that yields candidates wins:
//   0  --pid N                 exactly that process
//   1  --title T / --exe E     every java process whose window title contains T (case-insensitive) and whose image name contains E
//   2  window title starts with "Minecraft"   (vanilla, Fabric, Forge, NeoForge, Prism/MultiMC/ATLauncher instances ... they all keep the game's own title)
// Never selected on their own: a java process without a window (an IDE's background JVM, a Gradle daemon, a server) and a java window that is not
// titled "Minecraft" (an IDE, or a custom client such as Lunar): the agent is not loaded into a process the user did not mean. Such windows are LISTED when
// nothing matches, with the flag that selects one (--pid / --title). More than one candidate in the winning tier is an error, not "the first".
#pragma once
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

namespace procselect
{
    struct Candidate
    {
        unsigned long pid = 0;
        std::string title; // main window title, empty if none
        std::string exeName; // "javaw.exe"
    };

    struct Selector
    {
        unsigned long pid = 0;
        std::string title, exe; // case-insensitive substrings, empty = not given
        bool Any() const { return pid != 0 || !title.empty() || !exe.empty(); }
    };

    struct Result
    {
        enum class Kind { None, One, Ambiguous } kind = Kind::None;
        std::vector<size_t> idx; // indices into the candidate list: one for One, all of them for Ambiguous
        int tier = -1;
    };

    inline std::string Lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    inline bool Contains(const std::string& hay, const std::string& needle) { return Lower(hay).find(Lower(needle)) != std::string::npos; }

    inline Result Select(const std::vector<Candidate>& c, const Selector& sel)
    {
        Result r;
        auto pick = [&](int tier, auto pred) -> bool
        {
            std::vector<size_t> v;
            for (size_t i = 0; i < c.size(); ++i) if (pred(c[i])) v.push_back(i);
            if (v.empty()) return false;
            r.tier = tier;
            r.idx = std::move(v);
            r.kind = r.idx.size() == 1 ? Result::Kind::One : Result::Kind::Ambiguous;
            return true;
        };
        if (sel.pid != 0)
        {
            pick(0, [&](const Candidate& x) { return x.pid == sel.pid; });
            return r;
        }
        if (!sel.title.empty() || !sel.exe.empty())
        {
            pick(1, [&](const Candidate& x) { return (sel.title.empty() || Contains(x.title, sel.title)) && (sel.exe.empty() || Contains(x.exeName, sel.exe)); });
            return r;
        }
        pick(2, [](const Candidate& x) { return x.title.rfind("Minecraft", 0) == 0; });
        return r;
    }

    // One user-facing sentence (HUD / console) for a result that is not One. Empty for One.
    inline std::string Explain(const std::vector<Candidate>& c, const Selector& sel, const Result& r)
    {
        if (r.kind == Result::Kind::One) return {};
        if (r.kind == Result::Kind::None)
        {
            if (sel.pid != 0) return "no java process with PID " + std::to_string(sel.pid) + " (see --list)";
            if (!sel.title.empty() || !sel.exe.empty()) return "no java process matches" + (sel.title.empty() ? std::string() : " --title \"" + sel.title + "\"") + (sel.exe.empty() ? std::string() : " --exe \"" + sel.exe + "\"") + " (see --list)";
            std::string others;
            for (const auto& x : c) if (!x.title.empty()) others += (others.empty() ? "" : ", ") + std::string("PID ") + std::to_string(x.pid) + " '" + x.title + "'";
            if (others.empty()) return "waiting for minecraft";
            return "no window titled Minecraft; other Java windows: " + others + " - if one of them is the game, choose it with --pid <PID> or --title <text>";
        }
        std::string l;
        for (size_t i : r.idx) l += (l.empty() ? "" : ", ") + std::string("PID ") + std::to_string(c[i].pid) + (c[i].title.empty() ? "" : " '" + c[i].title + "'");
        return std::to_string(r.idx.size()) + " Minecraft clients" + " found: " + l + " - choose one with --pid <PID> or --title <text>";
    }

    // Short advice for a failed attach, from the text of the failure (the helper's first output line or the injector's own message).
    // Only patterns this code base itself produces, or class names that are certain (AttachNotSupportedException), are matched.
    inline std::string AttachHint(const std::string& failure)
    {
        auto has = [&](const char* s) { return Contains(failure, s); };
        if (has("access is denied") || has("openprocess") || has("privilege"))
            return "run mcesp at the same privilege level as the game (both normal, or both as administrator)";
        if (has("no attach api") || has("jdk.attach"))
            return "the Java that runs the game has no attach support: install a JDK 17+ and pass --java <JDK>\\bin\\java.exe";
        if (has("agent jar not found")) return "build the agent (agent\\build.bat) or pass --jar <path>";
        if (has("no java.exe found")) return "install a JDK 17+ or pass --java <path>\\bin\\java.exe";
        if (has("attachnotsupportedexception") || has("attach failed"))
            return "the game's JVM refused the attach (a launcher or a JVM flag such as -XX:+DisableAttachMechanism can do that, as can another user account); "
                   "use the manual route: add the -javaagent line printed by 'mcesp --no-inject' to the game's JVM arguments";
        return "see esp_log.txt; the manual route is 'mcesp --no-inject' and the -javaagent line it prints";
    }
} // namespace procselect
