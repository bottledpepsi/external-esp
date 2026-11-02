// agent_jar_test - SHA-256 against independently computed values (Python hashlib), extraction / reuse / repair of the embedded jar, clean-up.
#include "AgentJar.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>
namespace fs = std::filesystem;
using namespace agentjar;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

static std::string Get(const fs::path& p) { std::ifstream in(p, std::ios::binary); std::stringstream s; s << in.rdbuf(); return s.str(); }
static void Put(const fs::path& p, const std::string& t) { fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << t; }
static std::vector<unsigned char> Bytes(size_t n, int seed = 7) { std::vector<unsigned char> v(n); for (size_t i = 0; i < n; ++i) v[i] = static_cast<unsigned char>((i * seed + 3) % 256); return v; }

int main()
{
    // ---- SHA-256 ----
    CHECK(Sha256Hex("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(Sha256Hex("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    {
        std::string m(1000000, 'a');
        CHECK(Sha256Hex(m.data(), m.size()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
        // the same text fed in awkward pieces gives the same hash
        Sha256 h;
        for (size_t i = 0; i < m.size(); i += 777) h.Update(m.data() + i, std::min<size_t>(777, m.size() - i));
        std::string hex; for (uint8_t b : h.Final()) { char x[3]; std::snprintf(x, 3, "%02x", b); hex += x; }
        CHECK(hex == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    }
    struct V { size_t n; const char* hex; };
    for (const V& v : std::vector<V>{
        {55, "e7313d333c272e639f790978283f9eb392e843d0f29b7016828bb1daa4aac70b"},
        {56, "4324d65f3c103567f5589c710bc08f8523f929a9272e3af36fc968e52abc6c27"},
        {57, "35df609437dcfea3279283ab79fd554e2bf78f8f7ae2de532d8ee300b09e8f73"},
        {63, "81c80242132f230c3bd41b3e63bbcff16107339549214a99614ff26664625055"},
        {64, "39e3d7b6b5d075d37d053ad89b24b41bef4f3c29760c84447cab3f3be1882241"},
        {65, "aacca6ff74fdbb296d165a45cecfa04e5127bc008770fbbdd48006f2d2fae95e"},
        {119, "9ce7368e4daf32341631b492e80359dc9f594b48453cd0dd5bf0b19279cc177e"},
        {120, "7836b787757e95e58b3ca5aec90b1b004e8deba1e50e9675af9cabf1a13a04b5"},
        {128, "d2742f1f4ac6bb7ca2b239ee18402ba8b3f9f8e652d2a72973c2b9ba11c08cf6"},
        {1000, "1e9bc38cbf860b9ec31918b065f9b52476c549a782e0e7990bed8ce3868d2371"},
    })
    {
        const auto b = Bytes(v.n);
        CHECK(Sha256Hex(b.data(), b.size()) == v.hex);   // lengths around the 55/56/64-byte padding edges
    }

    const fs::path root = fs::current_path() / "jartest";
    fs::remove_all(root);
    const auto bytes = Bytes(5000, 11);
    const Blob blob{bytes.data(), bytes.size()};
    const std::string hash = Sha256Hex(bytes.data(), bytes.size());

    // ---- extraction: created, reused, repaired ----
    CHECK(FileNameFor(hash) == "esp-agent-" + hash.substr(0, 12) + ".jar" && hash.size() == 64);
    Extracted r = EnsureExtracted(blob, root / "agent");
    CHECK(r.how == Extracted::How::Created && r.hash == hash && r.path.filename() == FileNameFor(hash));
    CHECK(Get(r.path) == std::string(bytes.begin(), bytes.end()));
    CHECK(!fs::exists(r.path.string() + ".tmp"));
    r = EnsureExtracted(blob, root / "agent");
    CHECK(r.how == Extracted::How::Reused);
    Put(r.path, "truncated");                                          // damaged file under the right name
    r = EnsureExtracted(blob, root / "agent");
    CHECK(r.how == Extracted::How::Repaired && HashFileHex(r.path) == hash);
    { std::string s = Get(r.path); s[100] ^= 1; Put(r.path, s); }       // one flipped bit, same size
    r = EnsureExtracted(blob, root / "agent");
    CHECK(r.how == Extracted::How::Repaired && HashFileHex(r.path) == hash);
    fs::remove(r.path);
    CHECK(EnsureExtracted(blob, root / "agent").how == Extracted::How::Created);

    // ---- a different jar gets a different file; the old one is untouched ----
    const auto bytes2 = Bytes(5001, 13);
    const Blob blob2{bytes2.data(), bytes2.size()};
    const Extracted r2 = EnsureExtracted(blob2, root / "agent");
    CHECK(r2.how == Extracted::How::Created && r2.path != r.path && fs::exists(r.path) && HashFileHex(r.path) == hash);

    // ---- failures are reported, not thrown ----
    CHECK(EnsureExtracted(Blob{}, root / "agent").how == Extracted::How::Failed);
    Put(root / "file", "x");
    const Extracted bad = EnsureExtracted(blob, root / "file" / "sub");   // the folder cannot be created (a file is in the way)
    CHECK(bad.how == Extracted::How::Failed && !bad.error.empty());

    // ---- clean-up: everything of ours except the current one ----
    {
        const fs::path d = root / "agent";
        Put(d / "esp-agent-deadbeef0000.jar", "old");
        Put(d / "esp-agent-111111111111.jar.tmp", "stray");
        Put(d / "notes.txt", "mine");
        Put(d / "esp_agent.log", "log");
        const Cleaned c = CleanOld(d, FileNameFor(hash));
        CHECK(c.removed >= 3 && c.kept == 0);                               // the old jar, the stray temp, and the other version (r2)
        CHECK(fs::exists(d / FileNameFor(hash)) && fs::exists(d / "notes.txt") && fs::exists(d / "esp_agent.log"));
        CHECK(!fs::exists(d / "esp-agent-deadbeef0000.jar") && !fs::exists(r2.path));
        CHECK(CleanOld(d, FileNameFor(hash)).removed == 0);
        CHECK(CleanOld(root / "nonexistent", "x").removed == 0);            // no folder: nothing happens
    }

    fs::remove_all(root);
    if (fails) std::printf("%d failure(s)\n", fails); else std::puts("agent_jar_test: all passed");
    return fails ? 1 : 0;
}
