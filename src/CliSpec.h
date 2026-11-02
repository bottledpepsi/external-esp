// CliSpec.h - the command line in one table: every option with its argument, range and help text. --help is generated from it, and a check
// runs on the arguments before anything else starts (unknown option -> "did you mean", missing or bad value -> which and why). Portable.
// The meaning of each option stays in main.cpp; tests/cli_spec_test.cpp checks that the two lists agree.
#pragma once
#include <string>
#include <vector>

namespace cli
{
    enum class Arg { None, Required, Optional };
    enum class Kind { Any, Int, Word };

    struct Flag
    {
        const char* name; // "--port"
        const char* alias; // "-h" or ""
        Arg arg;
        Kind kind;
        const char* meta; // "N", "FILE", "sleep|hybrid"
        long long min, max; // Kind::Int
        const char* group;
        const char* help;
    };
    const std::vector<Flag>& Flags();
    const Flag* Find(const std::string& name);

    struct Check
    {
        enum class Result { Ok, Help, Error } result = Result::Ok;
        std::string message; // for Error: what is wrong and what to do; one or two lines
    };
    // argv[0] is skipped. Does not look at the system or the files.
    Check Preflight(int argc, const char* const* argv);

    std::string Suggest(const std::string& unknown); // closest option within 2 edits (or a shared prefix), "" if none
    std::string Help(); // the whole --help text, from the table

    // The plain-words reason a UDP bind failed (WSAGetLastError()) and what to do about it.
    std::string DescribePortError(unsigned port, int wsaError);
} // namespace cli
