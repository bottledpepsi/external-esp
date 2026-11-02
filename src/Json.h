// Json.h - tiny JSON reader/writer, just enough for esp_config.json, friends.json and profiles/*.json
// (flat objects of string/number/bool values, and arrays of strings). No external dependency.
// Unknown keys are kept in the map and simply ignored by callers, like System.Text.Json does.
#pragma once
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace json
{
    struct Value
    {
        enum class Type { Null, Bool, Number, String } type = Type::Null;

        bool b = false;
        double n = 0.0;
        std::string s;
        // The value exactly as it was written in the file (a string with its quotes, a nested object/array in full). ParseObject fills it for
        // every value, so a caller can write back a key it does not understand unchanged.
        std::string raw;
    };

    using Object = std::map<std::string, Value>;

    // Parses a top-level object. Nested objects/arrays inside values are skipped (value -> Null).
    std::optional<Object> ParseObject(std::string_view text, std::string* error = nullptr);
    // Parses a top-level array of strings (non-string elements are skipped).
    std::optional<std::vector<std::string>> ParseStringArray(std::string_view text, std::string* error = nullptr);

    // True when `text` is exactly one well-formed JSON value (RFC 8259: strict numbers, escapes, no trailing text; nesting depth <= 64).
    // ParseObject is lenient about what it skips; this is what decides whether a skipped value may be written back out.
    bool IsValid(std::string_view text);

    std::string Escape(std::string_view s);
    std::string Number(double v); // 2 -> "2", 0.75 -> "0.75" (no trailing zeros)

    // Convenience getters with fallbacks (missing key / wrong type -> fallback).
    std::string GetString(const Object& o, const char* key, const std::string& fallback);
    double GetNumber(const Object& o, const char* key, double fallback);
    bool GetBool(const Object& o, const char* key, bool fallback);
} // namespace json
