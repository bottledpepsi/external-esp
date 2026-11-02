// cli_spec_test - the command-line table: preflight (unknown option + suggestion, missing / bad values), generated help, port error texts,
// and that the table and the option handling in src/main.cpp name the same options.
#include "CliSpec.h"
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
using namespace cli;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

static Check Run(std::vector<const char*> args)
{
    args.insert(args.begin(), "mcesp");
    return Preflight(static_cast<int>(args.size()), args.data());
}
static bool Fails(std::vector<const char*> args, const char* part)
{
    const Check c = Run(args);
    return c.result == Check::Result::Error && c.message.find(part) != std::string::npos;
}

int main()
{
    // ---- fine ----
    CHECK(Run({}).result == Check::Result::Ok);
    CHECK(Run({"--demo", "--port", "47616", "--hz", "90", "--profile", "low-power", "--open-config"}).result == Check::Result::Ok);
    CHECK(Run({"--selftest"}).result == Check::Result::Ok && Run({"--selftest", "30"}).result == Check::Result::Ok);
    CHECK(Run({"--selftest", "--demo"}).result == Check::Result::Ok);                 // the optional value is not taken from an option
    CHECK(Run({"--diagnostics", "report.txt"}).result == Check::Result::Ok && Run({"--diagnostics"}).result == Check::Result::Ok);
    CHECK(Run({"--agent-pacer", "hybrid", "-v"}).result == Check::Result::Ok);
    CHECK(Run({"--simulate-device-loss=persistent"}).result == Check::Result::Ok);
    CHECK(Run({"--java", "C:\\Program Files\\jdk\\bin\\java.exe", "--title", "My Client"}).result == Check::Result::Ok);
    CHECK(Run({"--help"}).result == Check::Result::Help && Run({"-h"}).result == Check::Result::Help);

    // ---- unknown option: a suggestion when there is a close one ----
    {
        const Check c = Run({"--prot", "5"});
        CHECK(c.result == Check::Result::Error && c.message == "Unknown option '--prot'. Did you mean '--port'?");
        CHECK(Fails({"--selftst"}, "Did you mean '--selftest'?"));
        CHECK(Fails({"--replay-lop"}, "Did you mean '--replay-loop'?"));
        CHECK(Fails({"--rep"}, "Did you mean"));                                         // a prefix
        CHECK(Fails({"--completely-different-thing"}, "Unknown option") && Run({"--completely-different-thing"}).message.find("Did you mean") == std::string::npos);
        CHECK(Fails({"banana"}, "Unexpected argument 'banana'"));
    }
    // ---- missing values ----
    CHECK(Fails({"--port"}, "--port needs a value (N)."));
    CHECK(Fails({"--port", "--demo"}, "--port needs a value"));                          // the next word is an option
    CHECK(Fails({"--profile"}, "--profile needs a value (NAME)."));
    CHECK(Fails({"--agent-pacer"}, "needs a value (sleep|hybrid)"));
    // ---- bad values ----
    CHECK(Fails({"--port", "abc"}, "--port: 'abc' is not a whole number from 1 to 65535."));
    CHECK(Fails({"--port", "0"}, "from 1 to 65535") && Fails({"--port", "70000"}, "from 1 to 65535") && Fails({"--port", "47615x"}, "not a whole number"));
    CHECK(Fails({"--hz", "500"}, "from 1 to 240") && Fails({"--hz", "-5"}, "--hz: '-5' is not a whole number from 1 to 240"));
    CHECK(Fails({"--pid", "0"}, "from 1 to") && Run({"--pid", "4242"}).result == Check::Result::Ok);
    CHECK(Fails({"--selftest", "0"}, "from 1 to 600") && Fails({"--pacing-report", "99999"}, "from 1 to 3600"));
    CHECK(Fails({"--agent-pacer", "fast"}, "'fast' is not sleep or hybrid"));
    CHECK(Run({"--port", "1"}).result == Check::Result::Ok && Run({"--port", "65535"}).result == Check::Result::Ok);

    // ---- the table itself ----
    {
        std::set<std::string> names;
        for (const Flag& f : Flags())
        {
            CHECK(names.insert(f.name).second);                                           // no duplicates
            CHECK(std::string(f.name).rfind("--", 0) == 0 && *f.help && *f.group);
            if (f.arg != Arg::None) CHECK(*f.meta);
            if (f.kind == Kind::Int) CHECK(f.min <= f.max);
            CHECK(Find(f.name) == &f);
        }
        CHECK(Find("-v") == Find("--verbose") && Find("-h") == Find("--help") && !Find("--nope"));
    }

    // ---- the help is generated from the table: every option appears, nothing is cut off ----
    {
        const std::string h = Help();
        for (const Flag& f : Flags()) CHECK(h.find(f.name) != std::string::npos);
        CHECK(h.find("--port N") != std::string::npos && h.find("--selftest [S]") != std::string::npos && h.find("-v") != std::string::npos);
        CHECK(h.find("sleep|hybrid") != std::string::npos);
        CHECK(h.find("docs/cli.md") != std::string::npos);
        std::stringstream ss(h);
        std::string line;
        size_t longest = 0;
        while (std::getline(ss, line)) longest = std::max(longest, line.size());
        CHECK(longest <= 110);                                                            // wrapped, fits a normal console
    }

    // ---- port errors in plain words ----
    {
        CHECK(DescribePortError(47615, 10048).find("already in use by another program") != std::string::npos);
        CHECK(DescribePortError(47615, 10048).find("--port <number>") != std::string::npos);
        CHECK(DescribePortError(47615, 10013).find("excludedportrange") != std::string::npos);
        CHECK(DescribePortError(47615, 99).find("Windows error 99") != std::string::npos);
    }

    // ---- the table and main.cpp agree (a new option must be in both) ----
    {
        std::ifstream in(std::string(ESP_SRC_DIR) + "/main.cpp");
        CHECK(static_cast<bool>(in));
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string src = ss.str();
        std::set<std::string> handled;
        for (size_t p = 0; (p = src.find("a == \"-", p)) != std::string::npos; p += 6)
        {
            const size_t e = src.find('"', p + 6);
            handled.insert(src.substr(p + 6, e - (p + 6)));
        }
        CHECK(handled.size() > 20);
        for (const std::string& n : handled)
            if (!Find(n)) { std::printf("  main.cpp handles %s but CliSpec has no entry\n", n.c_str()); ++fails; }
        for (const Flag& f : Flags())
            if (!handled.count(f.name)) { std::printf("  CliSpec lists %s but main.cpp does not handle it\n", f.name); ++fails; }
    }

    if (fails) std::printf("%d failure(s)\n", fails); else std::puts("cli_spec_test: all passed");
    return fails ? 1 : 0;
}
