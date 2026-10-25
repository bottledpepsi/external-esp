// Diagnostics: redaction (names, paths, ids, tokens), tails, the assembled report, and the whole thing against a fake log directory.
#include "Diagnostics.h"
#include "DiagnosticsApp.h"
#include "FriendList.h"
#include "Logger.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
static bool Has(const std::string& s, const std::string& n) { return s.find(n) != std::string::npos; }

using diag::Redactor;

int main()
{
    // ---- names: whole word, any case, stable labels ----
    {
        Redactor r;
        r.AddName("Notch");
        r.AddName("jeb_");
        r.AddName("Dinnerbone", "<friend>");
        r.AddName("ab");                                                   // too short: ignored
        CHECK(r.NameCount() == 3);
        CHECK(r.Apply("Notch killed JEB_ near dinnerbone") == "<player1> killed <player2> near <friend>");
        CHECK(r.Apply("[Notch] Notch's sword, NOTCH.") == "[<player1>] <player1>'s sword, <player1>.");
        CHECK(r.Apply("user=Notch;wire=5") == "user=<player1>;wire=5");
        CHECK(r.Apply("Notchy Notch2 xNotch Notch_ _Notch") == "Notchy Notch2 xNotch Notch_ _Notch");   // part of a longer word: untouched
        CHECK(r.Apply("ab and ab") == "ab and ab");
        r.AddName("notch");                                                // same name in another case: no second label
        CHECK(r.NameCount() == 3 && r.Apply("Notch") == "<player1>");
        // longest first
        Redactor r2;
        r2.AddName("Bob");
        r2.AddName("BobTheBuilder");
        CHECK(r2.Apply("BobTheBuilder and Bob") == "<player2> and <player1>");
        // non-ASCII names are matched byte-exact, ASCII neighbours are boundaries
        Redactor r3;
        r3.AddName("J\xC3\xBCrgen");
        CHECK(r3.Apply("hi J\xC3\xBCrgen!") == "hi <player1>!" && r3.Apply("hi J\xC3\xBCrgens") == "hi J\xC3\xBCrgens");
        // idempotent
        CHECK(r.Apply(r.Apply("Notch and Dinnerbone")) == r.Apply("Notch and Dinnerbone"));
        CHECK(r.Apply("").empty());
    }

    // ---- paths ----
    {
        Redactor r;
        r.AddPathPrefix("C:\\Users\\bob\\Games\\esp", "<exe dir>");
        r.AddPathPrefix("C:\\Users\\bob", "%USERPROFILE%");
        CHECK(r.Apply("jar: C:\\Users\\bob\\Games\\esp\\esp-agent.jar") == "jar: <exe dir>\\esp-agent.jar");       // longest prefix wins
        CHECK(r.Apply("C:/Users/bob/Games/esp/export") == "<exe dir>/export");                                       // forward slashes
        CHECK(r.Apply("c:\\users\\BOB\\Desktop\\x") == "%USERPROFILE%\\Desktop\\x");                                 // any case
        CHECK(r.Apply("C:\\Users\\carol\\AppData\\x D:\\Users\\dave\\y") == "C:\\Users\\<user>\\AppData\\x D:\\Users\\<user>\\y");   // users we were never told about
        CHECK(r.Apply("/home/eve/.minecraft") == "/home/<user>/.minecraft");
        CHECK(r.Apply("C:\\Users\\<user>\\x") == "C:\\Users\\<user>\\x");                                            // already redacted
        CHECK(r.Apply("\"C:\\Users\\frank\"") == "\"C:\\Users\\<user>\"");                                           // quoted
        CHECK(r.Apply("C:\\Program Files\\Java") == "C:\\Program Files\\Java");                                      // other folders are fine
        Redactor trailing;
        trailing.AddPathPrefix("C:\\Users\\gina\\", "%USERPROFILE%");                                               // a trailing separator is ignored
        CHECK(trailing.Apply("C:\\Users\\gina\\x") == "%USERPROFILE%\\x");
        Redactor none;
        none.AddPathPrefix("", "x");
        none.AddPathPrefix("C", "x");
        CHECK(none.Apply("C is a letter") == "C is a letter");
    }

    // ---- ids and tokens ----
    {
        Redactor r;
        r.AddSecret("0123456789abcdef0123456789abcdef");
        r.AddSecret("short");                                              // under 8 characters: not a secret worth matching
        CHECK(r.Apply("K 0123456789abcdef0123456789abcdef") == "K <token>");
        CHECK(r.Apply("-javaagent:x.jar=port=1;token=ffffffffffffffffffffffffffffffff;hz=2") == "-javaagent:x.jar=port=1;token=<token>;hz=2");
        CHECK(r.Apply("token=abc") == "token=abc" && r.Apply("short") == "short");
        CHECK(r.Apply("id 069a79f4-44e9-4726-a5be-fca90e38aaf5 and 069A79F4-44E9-4726-A5BE-FCA90E38AAF5.") == "id <uuid> and <uuid>.");
        CHECK(r.Apply("not-a-uuid 069a79f4-44e9-4726-a5be-fca90e38aaf") == "not-a-uuid 069a79f4-44e9-4726-a5be-fca90e38aaf");   // one digit short
        CHECK(r.Apply("x069a79f4-44e9-4726-a5be-fca90e38aaf5") == "x069a79f4-44e9-4726-a5be-fca90e38aaf5");                        // glued to a word
    }

    // ---- learning names from the log lines the programs write ----
    {
        Redactor r;
        const std::string log = "12:00:00.000 [INFO ] [agent] Local username: Steve_99\n12:00:01.000 [INFO ] [agent] Found GameProfile (loader x)\n"
                                "-javaagent:esp-agent.jar=port=1;user=Alex;wire=5\n[INFO ] [agent] Found com.mojang.authlib.GameProfile (loader y)\n"
                                "Local username: (unknown - will use shallowest player object)\n";
        const size_t n = r.HarvestNames(log);
        CHECK(n == 2 && r.NameCount() == 2);                                  // Steve_99 and Alex; "Found GameProfile", class names and "(unknown" are not names
        const std::string out = r.Apply(log);
        CHECK(!Has(out, "Steve_99") && !Has(out, "Alex") && Has(out, "Local username: <player1>") && Has(out, "user=<player2>"));
        CHECK(Has(out, "Found GameProfile (loader x)") && Has(out, "com.mojang.authlib.GameProfile") && Has(out, "(unknown - will use"));
        CHECK(r.HarvestNames(log) == 0);                                       // nothing new the second time
    }

    // ---- tails ----
    {
        CHECK(diag::TailLines("a\nb\nc\nd\n", 2) == "c\nd\n" && diag::TailLines("a\nb\nc\nd", 2) == "c\nd" && diag::TailLines("a\nb\n", 5) == "a\nb\n");
        CHECK(diag::TailLines("", 3).empty() && diag::TailLines("a\n", 0).empty() && diag::TailLines("only", 1) == "only" && diag::TailLines("a\nb", 1) == "b");
        namespace fs = std::filesystem;
        const fs::path dir = fs::current_path() / "diagtest";
        fs::create_directories(dir);
        {
            std::ofstream f(dir / "big.log", std::ios::binary);
            for (int i = 0; i < 20000; ++i) f << "line " << i << " padding padding padding padding\n";     // ~700 KB: only the last 256 KiB are read
        }
        const std::string t = diag::ReadTail((dir / "big.log").string(), 5);
        CHECK(t.find("line 19999 ") != std::string::npos && t.find("line 19995 ") != std::string::npos && t.find("line 19994 ") == std::string::npos);
        CHECK(diag::ReadTail((dir / "missing.log").string(), 5).empty());
        { std::ofstream f(dir / "small.log", std::ios::binary); f << "x\ny\n"; }
        CHECK(diag::ReadTail((dir / "small.log").string(), 10) == "x\ny\n");
    }

    // ---- the assembled report ----
    {
        Redactor r;
        r.AddName("Notch", "<local>");
        r.AddName("Herobrine");
        r.AddSecret("0123456789abcdef0123456789abcdef");
        r.AddPathPrefix("C:\\Users\\bob", "%USERPROFILE%");
        diag::Input in;
        in.generated = "2026-10-23 14:03:22";
        in.version = "v1.0-5-gabc123";
        in.compiler = "GCC 15.2.0";
        in.os = "Windows 11 build 22631";
        in.cpu = "16 logical processors";
        in.renderer = "GPU (D3D11 feature level 0xB100)";
        in.settings = "port = 47615\nagent hz = 120\n";
        in.hudStatus = "agent ok: Herobrine is near Notch | UDP: 3 lost";
        in.streamStats = "published 1000";
        for (int i = 0; i < 400; ++i) in.espLog += "line " + std::to_string(i) + " Notch at C:\\Users\\bob\\x token=0123456789abcdef0123456789abcdef\n";
        in.agentLog = "Local username: Notch\nuuid 069a79f4-44e9-4726-a5be-fca90e38aaf5\n";
        const std::string rep = diag::Build(in, r);
        CHECK(Has(rep, "=== external-esp diagnostics") && Has(rep, "version:   v1.0-5-gabc123") && Has(rep, "compiler:  GCC 15.2.0") && Has(rep, "os:        Windows 11 build 22631"));
        CHECK(Has(rep, "--- settings ---\nport = 47615") && Has(rep, "--- HUD status ---") && Has(rep, "--- stream ---") && Has(rep, "--- esp_agent.log (last lines) ---"));
        CHECK(!Has(rep, "Notch") && !Has(rep, "Herobrine") && !Has(rep, "bob") && !Has(rep, "0123456789abcdef") && !Has(rep, "069a79f4"));
        CHECK(Has(rep, "<local>") && Has(rep, "<player1>") && Has(rep, "%USERPROFILE%") && Has(rep, "<token>") && Has(rep, "<uuid>"));
        CHECK(Has(rep, "line 399 ") && Has(rep, "line 250 ") && !Has(rep, "line 249 "));            // the last 150 lines of 400
        CHECK(rep.size() < diag::kMaxReportBytes);
        // empty parts are named, not omitted
        const std::string empty = diag::Build(diag::Input{}, Redactor{});
        CHECK(Has(empty, "--- esp_agent.log (last lines) ---\n(none)") && Has(empty, "--- HUD status ---\n(none)"));
        // the size limit
        diag::Input huge;
        huge.agentLog = std::string(2000000, 'x') + "\n";
        CHECK(diag::Build(huge, Redactor{}).size() <= diag::kMaxReportBytes);
        // a short log also shows the end of the previous one
        diag::Input two;
        two.espLog = "new 1\nnew 2\n";
        two.espLogBackupTail = "old 1\nold 2\n";
        CHECK(Has(diag::Build(two, Redactor{}), "--- esp_log.1.txt (end of the previous log) ---\nold 1"));
        diag::Input three;
        for (int i = 0; i < 300; ++i) three.espLog += "n\n";
        three.espLogBackupTail = "old 1\n";
        CHECK(!Has(diag::Build(three, Redactor{}), "old 1"));
    }

    // ---- end to end: real log files, friends, harvested names, the token ----
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::current_path() / "diagapp";
        fs::remove_all(dir);
        fs::create_directories(dir);
        elog::SetLogPath(dir / "esp_log.txt");
        elog::SetMaxBytes(1 << 20);
        LOG_INFO("Local player is Gertrude and the friend Wilhelmina is online; token=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        elog::Close();
        { std::ofstream f(dir / "esp_agent.log", std::ios::binary); f << "09:00:00.000 [INFO ] [agent] Local username: Gertrude\n09:00:01.000 [INFO ] [agent] path C:\\Users\\hans\\game\n"; }
        { std::ofstream f("friends.json"); f << "[\"Wilhelmina\"]"; }
        FriendList::Load();
        diag::AppContext ctx;
        ctx.version = "test";
        ctx.agentLog = dir / "esp_agent.log";
        ctx.hudStatus = "Archibald is here";
        ctx.playerNames = {"Archibald"};
        ctx.token = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        const std::string rep = diag::Collect(ctx);
        CHECK(!Has(rep, "Gertrude") && !Has(rep, "Wilhelmina") && !Has(rep, "Archibald") && !Has(rep, "hans") && !Has(rep, "aaaaaaaa"));
        CHECK(Has(rep, "version:   test") && Has(rep, "esp_log.txt (last lines)") && Has(rep, "<token>") && Has(rep, "C:\\Users\\<user>\\game"));
        CHECK(Has(rep, "Local player is <player") && Has(rep, "friend <player"));
        std::printf("--- sample report head ---\n%.600s\n--- end ---\n", rep.c_str());
    }

    std::printf(fails ? "\n%d FAILURES\n" : "\nALL OK\n", fails);
    return fails;
}
