// setup_checks_test - Java version parsing, JDK release files, and what each combination of facts tells the user.
#include "SetupChecks.h"
#include <cstdio>
using namespace setup;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

static const Item* Find(const std::vector<Item>& v, const char* name)
{
    for (const Item& i : v) if (i.name == name) return &i;
    return nullptr;
}
static Facts Good()
{
    Facts f;
    f.configDir = "C:\\Users\\x\\AppData\\Roaming\\external-esp";
    f.jarOk = true; f.jarSource = "embedded in this exe";
    f.java.found = true; f.java.path = "C:\\jdk-21\\bin\\java.exe"; f.java.major = 21; f.java.attach = JavaInfo::Attach::Yes;
    f.clients = 1; f.clientName = "javaw.exe (pid 4242)";
    return f;
}

int main()
{
    // ---- version strings ----
    CHECK(ParseJavaMajor("1.8.0_392") == 8 && ParseJavaMajor("\"1.8.0_392\"") == 8 && ParseJavaMajor("1.7.0_80") == 7);
    CHECK(ParseJavaMajor("17.0.1") == 17 && ParseJavaMajor("21") == 21 && ParseJavaMajor("25-ea") == 25 && ParseJavaMajor("9-internal") == 9 && ParseJavaMajor("11.0.22+7") == 11);
    CHECK(ParseJavaMajor("") == 0 && ParseJavaMajor("banana") == 0 && ParseJavaMajor("1.x") == 0 && ParseJavaMajor("1.12") == 0 && ParseJavaMajor("0.9") == 0);

    // ---- release files ----
    {
        const std::string jdk = "IMPLEMENTOR=\"Eclipse Adoptium\"\r\nJAVA_VERSION=\"21.0.2\"\r\nMODULES=\"java.base java.logging jdk.attach jdk.compiler\"\r\n";
        JavaInfo j = ParseRelease(jdk);
        CHECK(j.found && j.major == 21 && j.attach == JavaInfo::Attach::Yes);
        j = ParseRelease("JAVA_VERSION=\"17.0.9\"\nMODULES=\"java.base java.desktop jdk.unsupported\"\n"); // a jlinked runtime
        CHECK(j.major == 17 && j.attach == JavaInfo::Attach::No);
        j = ParseRelease("MODULES=\"jdk.attach\"\nJAVA_VERSION=\"21\"");                                        // first and only module
        CHECK(j.attach == JavaInfo::Attach::Yes && j.major == 21);
        j = ParseRelease("MODULES=\"jdk.attach.extra java.base\"");                                          // a different module name
        CHECK(j.attach == JavaInfo::Attach::No);
        j = ParseRelease("JAVA_VERSION=\"1.8.0_392\"\n");                                                   // Java 8: no MODULES line: unknown, not "no"
        CHECK(j.major == 8 && j.attach == JavaInfo::Attach::Unknown);
        j = ParseRelease("");
        CHECK(j.found && j.major == 0 && j.attach == JavaInfo::Attach::Unknown);
    }

    // ---- everything fine ----
    {
        const auto v = Evaluate(Good());
        CHECK(v.size() == 5 && !HasFailure(v) && !HasWarning(v));
        for (const Item& i : v) CHECK(i.status == Status::Ok && i.fix.empty());
        CHECK(Render(v, false).find("Everything needed is in place.") != std::string::npos);
        CHECK(Render(v, false).find("[ OK ] Settings folder") == 0);
    }
    // ---- each problem on its own ----
    {
        Facts f = Good(); f.configWritable = false;
        auto v = Evaluate(f);
        CHECK(Find(v, "Settings folder")->status == Status::Fail && HasFailure(v));

        f = Good(); f.jarOk = false;
        v = Evaluate(f);
        CHECK(Find(v, "Agent")->status == Status::Fail && Find(v, "Agent")->fix.find("agent\\build.bat") != std::string::npos);
        CHECK(Find(v, "Agent")->detail == "no agent jar at embedded in this exe" || Find(v, "Agent")->detail.find("no agent jar") == 0);

        f = Good(); f.portError = 10048;
        v = Evaluate(f);
        CHECK(Find(v, "UDP port")->status == Status::Fail && Find(v, "UDP port")->fix.find("--port <number>") != std::string::npos);

        f = Good(); f.java.found = false;
        v = Evaluate(f);
        CHECK(Find(v, "Java")->status == Status::Warn && !HasFailure(v) && Find(v, "Java")->fix.find("JDK") != std::string::npos);

        f = Good(); f.java.major = 6;
        v = Evaluate(f);
        CHECK(Find(v, "Java")->status == Status::Fail && Find(v, "Java")->detail.find("Java 6") != std::string::npos);

        f = Good(); f.java.attach = JavaInfo::Attach::No;
        v = Evaluate(f);
        CHECK(Find(v, "Java")->status == Status::Warn && Find(v, "Java")->detail.find("no jdk.attach") != std::string::npos);

        f = Good(); f.java.major = 0; f.java.attach = JavaInfo::Attach::Unknown;      // no release file: unknown is not a problem
        v = Evaluate(f);
        CHECK(Find(v, "Java")->status == Status::Ok && Find(v, "Java")->detail.find("version unknown") != std::string::npos);

        f = Good(); f.clients = 0;
        v = Evaluate(f);
        CHECK(Find(v, "Minecraft")->status == Status::Warn && !HasFailure(v) && Find(v, "Minecraft")->fix.find("start the game any time") != std::string::npos);
        f.clients = 3;
        v = Evaluate(f);
        CHECK(Find(v, "Minecraft")->status == Status::Warn && Find(v, "Minecraft")->fix.find("--pid") != std::string::npos);
    }
    // ---- rendering ----
    {
        Facts f = Good(); f.jarOk = false; f.clients = 0;
        const auto v = Evaluate(f);
        const std::string plain = Render(v, false), col = Render(v, true);
        CHECK(plain.find("[FAIL] Agent") != std::string::npos && plain.find("[WARN] Minecraft") != std::string::npos && plain.find("\x1b") == std::string::npos);
        CHECK(col.find("\x1b[31mFAIL\x1b[0m") != std::string::npos && col.find("\x1b[33mWARN\x1b[0m") != std::string::npos && col.find("\x1b[32m OK \x1b[0m") != std::string::npos);
        CHECK(plain.find("       Build it with agent") != std::string::npos);                   // the fix is indented under its item
        CHECK(plain.find("must be fixed") != std::string::npos);
    }

    if (fails) std::printf("%d failure(s)\n", fails); else std::puts("setup_checks_test: all passed");
    return fails ? 1 : 0;
}
