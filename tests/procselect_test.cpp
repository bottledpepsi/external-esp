// procselect_test.cpp - which Minecraft client is chosen, when selection must refuse, and the attach hints (src/ProcessSelect.h).
#include "../src/ProcessSelect.h"
#include <cstdio>

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

using namespace procselect;
using K = Result::Kind;

int main()
{
    // the situations from real machines
    Candidate vanilla{100, "Minecraft* 1.21.1", "javaw.exe"};
    Candidate fabric{101, "Minecraft* 1.20.4 - Singleplayer", "javaw.exe"};
    Candidate ide{200, "MyProject - IntelliJ IDEA", "java.exe"};
    Candidate gradle{201, "", "java.exe"}; // a daemon with no window
    Candidate lunar{300, "Lunar Client (1.8.9-abc/Genesis)", "javaw.exe"};
    Candidate server{202, "", "java.exe"};
    Selector none;

    // 1. one vanilla client among background JVMs: picked, the others never
    { auto r = Select({gradle, ide, vanilla, server}, none); CHECK(r.kind == K::One && r.idx[0] == 2 && r.tier == 2); }
    // 2. an IDE window and a background JVM, no game: NOTHING is selected (the agent must not go into the IDE), and the window is listed with the way to choose it
    { std::vector<Candidate> v{gradle, ide}; auto r = Select(v, none); CHECK(r.kind == K::None);
      std::string e = Explain(v, none, r); CHECK(e.find("no window titled Minecraft") == 0 && e.find("PID 200") != std::string::npos && e.find("PID 201") == std::string::npos && e.find("--title") != std::string::npos); }
    // 3. nothing but windowless JVMs: nothing selected, nothing to list
    { std::vector<Candidate> v{gradle, server}; auto r = Select(v, none); CHECK(r.kind == K::None && Explain(v, none, r) == "waiting for minecraft"); }
    // 4. a custom client alone: not guessed, but listed; --title or --pid picks it (case 8)
    { std::vector<Candidate> v{lunar, gradle}; auto r = Select(v, none); CHECK(r.kind == K::None && Explain(v, none, r).find("Lunar Client") != std::string::npos); }
    // 5. two clients: refuse, listing both
    { std::vector<Candidate> v{vanilla, fabric, ide}; auto r = Select(v, none); CHECK(r.kind == K::Ambiguous && r.idx.size() == 2 && r.tier == 2);
      std::string e = Explain(v, none, r); CHECK(e.find("2 Minecraft clients found") == 0 && e.find("PID 100") != std::string::npos && e.find("PID 101") != std::string::npos && e.find("--pid") != std::string::npos); }
    // 6. a Minecraft client wins over other Java windows
    { std::vector<Candidate> v{ide, lunar, vanilla}; auto r = Select(v, none); CHECK(r.kind == K::One && r.idx[0] == 2 && r.tier == 2); }
    // 7. --pid wins over everything, including a windowless process
    { Selector s; s.pid = 201; auto r = Select({vanilla, gradle}, s); CHECK(r.kind == K::One && r.idx[0] == 1 && r.tier == 0); }
    { Selector s; s.pid = 999; std::vector<Candidate> v{vanilla}; auto r = Select(v, s); CHECK(r.kind == K::None && Explain(v, s, r).find("PID 999") != std::string::npos); }
    // 8. --title picks one of two clients, case-insensitively, and a custom title works
    { Selector s; s.title = "singleplayer"; auto r = Select({vanilla, fabric}, s); CHECK(r.kind == K::One && r.idx[0] == 1 && r.tier == 1); }
    { Selector s; s.title = "lunar"; auto r = Select({vanilla, lunar}, s); CHECK(r.kind == K::One && r.idx[0] == 1); }
    // 9. --exe narrows; combined with --title both must match
    { Selector s; s.exe = "JAVAW"; auto r = Select({ide, vanilla, gradle}, s); CHECK(r.kind == K::One && r.idx[0] == 1); }
    { Selector s; s.exe = "java.exe"; s.title = "minecraft"; auto r = Select({vanilla, ide}, s); CHECK(r.kind == K::None); }
    { Selector s; s.title = "zzz"; std::vector<Candidate> v{vanilla}; auto r = Select(v, s); CHECK(r.kind == K::None && Explain(v, s, r).find("--title \"zzz\"") != std::string::npos); }
    // 10. nothing at all
    { auto r = Select({}, none); CHECK(r.kind == K::None && Explain({}, none, r) == "waiting for minecraft"); }
    // 11. the title prefix is case-sensitive like the game's: "minecraft" in a browser tab title does not count
    { Candidate tab{300, "how to minecraft - Chrome", "java.exe"}; auto r = Select({tab}, none); CHECK(r.kind == K::None); }
    // One yields no explanation
    { auto r = Select({vanilla}, none); CHECK(Explain({vanilla}, none, r).empty()); }

    // attach hints
    CHECK(AttachHint("OpenProcess(PID 12) failed (error 5)").find("privilege level") != std::string::npos);
    CHECK(AttachHint("Access is denied.").find("privilege level") != std::string::npos);
    CHECK(AttachHint("that Java has no Attach API (needs a full JDK)").find("JDK 17+") != std::string::npos);
    CHECK(AttachHint("that Java runtime lacks the jdk.attach module").find("--java") != std::string::npos);
    CHECK(AttachHint("attach failed: com.sun.tools.attach.AttachNotSupportedException: something").find("--no-inject") != std::string::npos);
    CHECK(AttachHint("agent jar not found: C:\\x\\esp-agent.jar").find("--jar") != std::string::npos);
    CHECK(AttachHint("something nobody has seen before").find("esp_log.txt") != std::string::npos);

    std::printf(g_fail == 0 ? "ALL OK (%d checks)\n" : "%d FAILURES\n", g_fail == 0 ? g_checks : g_fail);
    return g_fail == 0 ? 0 : 1;
}
