// SetupChecks.h - the first-run checklist: what the facts about this PC mean, in plain words, with what to do about each problem. Portable.
// SetupApp.cpp (Windows) collects the facts and asks the questions; the judgement lives here so it can be tested.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace setup
{
    enum class Status { Ok, Warn, Fail };

    // "1.8.0_392" -> 8, "17.0.1" -> 17, "21" -> 21, "25-ea" -> 25, "9-internal" -> 9; 0 when it cannot be read.
    int ParseJavaMajor(const std::string& version);

    struct JavaInfo
    {
        bool found = false;
        std::string path; // java.exe
        int major = 0; // 0 = unknown (no release file)
        enum class Attach { Unknown, Yes, No } attach = Attach::Unknown; // does this runtime have the jdk.attach module / tools.jar
    };
    // Reads a JDK's `release` file: JAVA_VERSION="..." and MODULES="java.base ... jdk.attach ...".
    JavaInfo ParseRelease(const std::string& releaseFileText);

    struct Facts
    {
        // config folder
        bool configWritable = true;
        std::string configDir;
        // agent
        bool jarOk = false;
        std::string jarSource; // "embedded in this exe", "esp-agent.jar next to the exe", "--jar"
        // UDP port
        unsigned port = 47615;
        int portError = 0; // WSA error of a bind probe, 0 = free
        // Java: the best candidate of those that will run the attach helper (JAVA_HOME, PATH); `found` false when there is none
        JavaInfo java;
        // Minecraft
        int clients = 0; // java/javaw processes that look like Minecraft
        std::string clientName; // when exactly one
    };

    struct Item
    {
        std::string name;
        Status status = Status::Ok;
        std::string detail; // what was found
        std::string fix; // what to do (empty when Ok)
    };

    std::vector<Item> Evaluate(const Facts& f);
    bool HasFailure(const std::vector<Item>& items);
    bool HasWarning(const std::vector<Item>& items);
    // The console text: one line per item ([ OK ] / [WARN] / [FAIL], coloured when `color`), the fix indented under it, and a closing sentence.
    std::string Render(const std::vector<Item>& items, bool color);
} // namespace setup
