// ConfigSchema.h - what a settings file may contain, and what to do when it contains something else.
//
//   * a schema VERSION ("Version" in the file; a file without it is version 0: the C# tool's, or a build older than this one),
//   * a table of every known key with its type, its allowed range or its allowed words, and (through EspConfig's initialisers) its default,
//   * Migrate(): one function per version step, run in order, so an old file is brought up to the current layout before it is read,
//   * Load(): migrate -> check every key -> build the EspConfig. Nothing in a file can make it throw, and every thing that was replaced is
//     reported in plain words ("BoxStyle: 'Fancy' is not one of ... - using Sharp").
// Keys this build does not know (a newer build wrote them, or the user added a note) are neither an error nor lost: they travel through
// EspConfig::Unknown and are written back on the next save.
// Portable (no Windows headers).
#pragma once
#include "EspConfig.h"
#include "Json.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace configschema
{
    // Bump this when a key is renamed, removed or changes meaning, and add the step to kSteps in ConfigSchema.cpp. Adding a key with a
    // default needs no bump: an older file simply does not have it.
    inline constexpr int kCurrentVersion = 1;

    struct Issue
    {
        enum class Level { Info, Warning };
        Level level = Level::Warning;
        std::string key; // "" = about the file as a whole
        std::string message;
    };

    struct LoadResult
    {
        EspConfig cfg; // always usable: every problem key holds its default or the nearest allowed value
        int fileVersion = 0; // what the file said; 0 = no "Version" key
        bool migrated = false;
        std::vector<Issue> issues;

        size_t Warnings() const;
        std::string Summary() const; // "version 0 -> 1, 2 warning(s)" for the log and the UI
    };

    // One migration step: takes the key/value object of version N and rewrites it to version N+1. It may add, rename, delete or recompute keys.
    using Step = std::function<void(json::Object&, std::vector<Issue>&)>;
    struct StepEntry
    {
        int from; // this step upgrades `from` -> `from + 1`
        Step fn;
    };

    // The real steps of this build (index = from-version).
    const std::vector<StepEntry>& Steps();

    // Runs the steps from `from` up to `to`. A missing step is reported and the object is left as it is (the keys are then read as they are).
    // Exposed with a `steps` parameter so the tests can run a chain of their own.
    void Migrate(json::Object& obj, int from, int to, const std::vector<StepEntry>& steps, std::vector<Issue>& issues);

    LoadResult Load(const json::Object& obj);
    // Parses `text` and loads it. `parsed` (when not null) says whether the text was a JSON object at all; if not, the result is the defaults
    // plus one warning carrying the parser's message.
    LoadResult LoadText(const std::string& text, bool* parsed = nullptr);

    bool IsKnownKey(const std::string& key); // "Version" and every key of EspConfig
    std::vector<std::string> KnownKeys(); // the table's keys, in file order (tests check them against ToJson)

    // A key's allowed range, for the UI and for the tests.
    struct Range
    {
        double min, max;
    };
    bool NumericRange(const std::string& key, Range& out);
} // namespace configschema
