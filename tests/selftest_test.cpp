// selftest_test.cpp - parsing of the agent's self-test report and the overlay's exit code decision (src/SelfTestReport.h).
#include "../src/SelfTestReport.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

int main(int argc, char** argv)
{
    // the sample is a real report written by the agent (agent_smoke.py selftest-fake): the contract between the two languages
    std::string path = ESP_TEST_DATA_DIR "/selftest_sample.txt";
    (void)argc; (void)argv;
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    selftest::Report r = selftest::Parse(ss.str());
    CHECK(r.valid);
    CHECK(r.exitCode == 0 && r.result == "PASS");
    CHECK(r.checks.size() >= 8);
    bool camera = false, items = false;
    for (auto& c : r.checks)
    {
        if (c.name == "camera") camera = c.level == "PASS" && c.text.find("exact camera") == 0;
        if (c.name == "items") items = c.level == "PASS";
    }
    CHECK(camera && items);
    bool sawAgent = false, sawMc = false;
    for (auto& h : r.header) { sawAgent |= h.rfind("agent: build ", 0) == 0; sawMc |= h.rfind("minecraft: ", 0) == 0; }
    CHECK(sawAgent && sawMc);

    // CRLF and truncated input
    CHECK(selftest::Parse("esp-selftest 1\r\nCHECK a PASS x y\r\nEXIT 2\r\n").exitCode == 2);
    CHECK(!selftest::Parse("esp-selftest 1\nCHECK a PASS x\n").valid);        // no EXIT line: the agent was cut off
    CHECK(!selftest::Parse("hello\nEXIT 0\n").valid);                           // not our format
    CHECK(!selftest::Parse("").valid);
    auto one = selftest::Parse("esp-selftest 1\nCHECK fov WARN no FOV method found: boxes drift\nCHECK x SKIP\nEXIT 2\n");
    CHECK(one.checks.size() == 2 && one.checks[0].level == "WARN" && one.checks[0].text == "no FOV method found: boxes drift" && one.checks[1].level == "SKIP" && one.checks[1].text.empty());

    // the decision table
    using namespace selftest;
    auto agent = [](int code) { Report a; a.valid = true; a.exitCode = code; return a; };
    OverlayFacts f;
    f.agent = agent(kOk);
    f.packets = 100;
    CHECK(Decide(f) == kOk);
    f.agent = agent(kSkips); CHECK(Decide(f) == kSkips);
    f.agent = agent(kWarn); CHECK(Decide(f) == kWarn);
    f.agent = agent(kFail); CHECK(Decide(f) == kFail);
    f.packets = 0; CHECK(Decide(f) == kNoData);                               // the agent is fine but nothing reached the overlay
    f.agent = agent(kNotMinecraft); CHECK(Decide(f) == kNotMinecraft);         // final: no UDP data can be expected
    f.agent = agent(kNotInWorld); CHECK(Decide(f) == kNotInWorld);
    f.packets = 50; f.agent = agent(kOk);
    OverlayFacts g = f; g.candidates = 0; CHECK(Decide(g) == kNotMinecraft);
    g = f; g.candidates = 2; CHECK(Decide(g) == kAmbiguous);
    g = f; g.udpBound = false; CHECK(Decide(g) == kPortInUse);
    g = f; g.attachOk = false; CHECK(Decide(g) == kAttachFailed);
    g = f; g.reportArrived = false; CHECK(Decide(g) == kNoReport);
    g = f; g.agent.valid = false; CHECK(Decide(g) == kNoReport);
    g = f; g.candidates = 2; g.attachOk = false; CHECK(Decide(g) == kAmbiguous);   // earlier causes win
    for (int c : {0, 1, 2, 3, 10, 11, 12, 13, 14, 15, 16}) CHECK(std::string(Describe(c)) != "unknown");

    std::printf(g_fail == 0 ? "ALL OK (%d checks)\n" : "%d FAILURES\n", g_fail == 0 ? g_checks : g_fail);
    return g_fail == 0 ? 0 : 1;
}
