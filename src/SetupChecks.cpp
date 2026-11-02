#include "SetupChecks.h"
#include "AgentJar.h"
#include "CliSpec.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace setup
{
    int ParseJavaMajor(const std::string& v)
    {
        std::string s = v;
        s.erase(std::remove(s.begin(), s.end(), '"'), s.end());
        if (s.empty() || !std::isdigit(static_cast<unsigned char>(s[0]))) return 0;
        char* end = nullptr;
        long first = std::strtol(s.c_str(), &end, 10);
        if (first == 1 && end && *end == '.') // 1.8.0_392: the old scheme, the number after "1."
        {
            const long second = std::strtol(end + 1, nullptr, 10);
            return second >= 5 && second <= 8 ? static_cast<int>(second) : 0;
        }
        return first >= 9 && first < 1000 ? static_cast<int>(first) : 0;
    }

    JavaInfo ParseRelease(const std::string& text)
    {
        JavaInfo j;
        j.found = true;
        std::stringstream ss(text);
        for (std::string line; std::getline(ss, line);)
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (line.rfind("JAVA_VERSION=", 0) == 0) j.major = ParseJavaMajor(line.substr(13));
            else if (line.rfind("MODULES=", 0) == 0)
            {
                std::string v = line.substr(8);
                v.erase(std::remove(v.begin(), v.end(), '"'), v.end());
                const std::string m = " " + v + " "; // whole module names only: "jdk.attach.extra" is not "jdk.attach"
                j.attach = m.find(" jdk.attach ") != std::string::npos ? JavaInfo::Attach::Yes : JavaInfo::Attach::No;
            }
        }
        return j;
    }

    std::vector<Item> Evaluate(const Facts& f)
    {
        std::vector<Item> v;
        auto add = [&](const char* name, Status s, std::string detail, std::string fix = {}) { v.push_back({name, s, std::move(detail), std::move(fix)}); };

        // Where the settings go
        if (f.configWritable) add("Settings folder", Status::Ok, f.configDir);
        else add("Settings folder", Status::Fail, f.configDir + " cannot be written", "Settings could not be saved. Check that the folder is not read-only and the disk is not full; then start again.");

        // The agent
        if (f.jarOk) add("Agent", Status::Ok, f.jarSource);
        else add("Agent", Status::Fail, f.jarSource.empty() ? "no agent jar" : "no agent jar at " + f.jarSource, "Build it with agent\\build.bat (needs a JDK) and build the exe again, or pass --jar <path to esp-agent.jar>.");

        // The UDP port
        if (f.portError == 0) add("UDP port", Status::Ok, std::to_string(f.port) + " is free");
        else add("UDP port", Status::Fail, std::to_string(f.port) + " cannot be used", cli::DescribePortError(f.port, f.portError));

        // Java for the attach helper
        if (!f.java.found)
            add("Java", Status::Warn, "no JDK found (JAVA_HOME / PATH)",
                "The attach helper first tries the game's own Java, which often has no Attach API. If attaching fails later, install a JDK 17+ (any vendor) or pass --java <path to java.exe>.");
        else if (f.java.major != 0 && f.java.major < 8)
            add("Java", Status::Fail, "Java " + std::to_string(f.java.major) + " at " + f.java.path, "Too old for the agent (needs Java 8 or newer). Install a current JDK or pass --java <path to java.exe>.");
        else if (f.java.attach == JavaInfo::Attach::No)
            add("Java", Status::Warn, "Java " + (f.java.major ? std::to_string(f.java.major) : std::string("?")) + " at " + f.java.path + " has no jdk.attach module",
                "This is a runtime without the Attach API (a JRE or a trimmed image). Install a full JDK or pass --java <path to a JDK's java.exe>.");
        else
            add("Java", Status::Ok, "Java " + (f.java.major ? std::to_string(f.java.major) : std::string("(version unknown)")) + " at " + f.java.path);

        // Minecraft
        if (f.clients == 0) add("Minecraft", Status::Warn, "not running", "Fine for now: start the game any time, the overlay attaches by itself.");
        else if (f.clients == 1) add("Minecraft", Status::Ok, f.clientName.empty() ? "1 client running" : f.clientName);
        else add("Minecraft", Status::Warn, std::to_string(f.clients) + " clients running", "Tell the overlay which one: --pid <process id> (see mcesp --list) or --title <part of the window title>.");
        return v;
    }

    bool HasFailure(const std::vector<Item>& items)
    {
        for (const Item& i : items) if (i.status == Status::Fail) return true;
        return false;
    }
    bool HasWarning(const std::vector<Item>& items)
    {
        for (const Item& i : items) if (i.status == Status::Warn) return true;
        return false;
    }

    std::string Render(const std::vector<Item>& items, bool color)
    {
        auto tag = [&](Status s)
        {
            const char* text = s == Status::Ok ? " OK " : s == Status::Warn ? "WARN" : "FAIL";
            if (!color) return std::string("[") + text + "]";
            const char* c = s == Status::Ok ? "32" : s == Status::Warn ? "33" : "31";
            return std::string("[\x1b[") + c + "m" + text + "\x1b[0m]";
        };
        std::string out;
        for (const Item& i : items)
        {
            std::string name = i.name;
            name.resize(std::max<size_t>(name.size(), 16), ' ');
            out += tag(i.status) + " " + name + i.detail + "\n";
            if (!i.fix.empty())
            {
                std::stringstream ss(i.fix);
                for (std::string line; std::getline(ss, line);) out += "       " + line + "\n";
            }
        }
        out += HasFailure(items) ? "\nSomething above must be fixed before the overlay can work.\n"
            : HasWarning(items)  ? "\nNothing blocks you; the warnings say what to look at if attaching fails.\n"
                                 : "\nEverything needed is in place.\n";
        return out;
    }
} // namespace setup
