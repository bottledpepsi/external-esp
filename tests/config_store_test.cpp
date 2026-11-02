// config_store_test - crash-safe writes, the last-good backup, quarantine and recovery of damaged settings files, and the debounced save.
#include "ConfigStore.h"
#include "EspConfig.h"
#include "FriendList.h"
#include "Paths.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
namespace fs = std::filesystem;
using namespace configstore;
using namespace std::chrono_literals;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

static void Put(const fs::path& p, const std::string& t) { fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << t; }
static std::string Get(const fs::path& p) { std::ifstream in(p, std::ios::binary); std::stringstream s; s << in.rdbuf(); return s.str(); }
static int CountCorrupt(const fs::path& dir)
{
    int n = 0;
    for (const auto& e : fs::directory_iterator(dir)) if (e.path().filename().string().find(".corrupt-") != std::string::npos) ++n;
    return n;
}

int main()
{
    const fs::path root = fs::current_path() / "storetest";
    fs::remove_all(root);
    const fs::path f = root / "sub" / "c.json";

    // ---- write: creates the folder, leaves no temp file, keeps the previous good file as .bak ----
    std::string err;
    CHECK(WriteAtomic(f, "{\"a\":1}", &err));
    CHECK(Get(f) == "{\"a\":1}" && !fs::exists(f.string() + ".tmp") && !fs::exists(f.string() + ".bak"));
    CHECK(WriteAtomic(f, "{\"a\":2}", &err));
    CHECK(Get(f) == "{\"a\":2}" && Get(f.string() + ".bak") == "{\"a\":1}");
    // a damaged current file must not replace a good backup
    Put(f, "{broken");
    CHECK(WriteAtomic(f, "{\"a\":3}", &err));
    CHECK(Get(f.string() + ".bak") == "{\"a\":1}" && Get(f) == "{\"a\":3}");
    WriteOptions nb; nb.keepBackup = false;
    const fs::path g = root / "g.json";
    CHECK(WriteAtomic(g, "{}", &err, nb) && WriteAtomic(g, "{\"x\":0}", &err, nb) && !fs::exists(g.string() + ".bak"));

    // ---- a failed write leaves the old file alone ----
    {
        const fs::path blocked = root / "blocked.json";
        fs::create_directories(blocked); // a directory where the file should go: the replace must fail
        CHECK(!WriteAtomic(blocked, "{}", &err) && !err.empty());
        CHECK(!fs::exists(blocked.string() + ".tmp"));
        fs::remove_all(blocked);
    }

    // ---- crash at each stage: old file intact, then a normal read cleans up ----
    for (Stage st : {Stage::TempWritten, Stage::BackupMade})
    {
        Put(f, "{\"v\":\"old\"}");
        SetFaultHook([st](Stage s) { return s != st; });
        CHECK(!WriteAtomic(f, "{\"v\":\"new\"}", &err));
        SetFaultHook({});
        CHECK(Get(f) == "{\"v\":\"old\"}");          // never half of each
        CHECK(fs::exists(f.string() + ".tmp"));       // the dead process left it
        const ReadResult r = Read(f, IsJsonObject);
        CHECK(r.status == ReadStatus::Ok && r.text == "{\"v\":\"old\"}");
        CHECK(!fs::exists(f.string() + ".tmp"));      // ... and the next start removes it
    }

    // ---- read: missing / ok ----
    CHECK(Read(root / "none.json", IsJsonObject).status == ReadStatus::Missing);

    // ---- damaged file with a good backup: restored, damaged one kept ----
    {
        fs::remove_all(root / "r");
        const fs::path c = root / "r" / "esp_config.json";
        CHECK(WriteAtomic(c, "{\"k\":1}", &err) && WriteAtomic(c, "{\"k\":2}", &err)); // .bak = k:1
        Put(c, "{\"k\":2,\"half\":");                                                   // cut off mid-write by something else
        ReadResult r = Read(c, IsJsonObject);
        CHECK(r.status == ReadStatus::RestoredFromBackup && r.text == "{\"k\":1}");
        CHECK(Get(c) == "{\"k\":1}");                                                    // main file is good again
        CHECK(CountCorrupt(c.parent_path()) == 1 && Get(r.quarantined) == "{\"k\":2,\"half\":");
        CHECK(r.note.find("last good copy was restored") != std::string::npos && r.note.find("esp_config.corrupt-") != std::string::npos);
        CHECK(Read(c, IsJsonObject).status == ReadStatus::Ok);
    }
    // ---- damaged and no backup / backup damaged too / empty / binary / wrong kind ----
    {
        fs::remove_all(root / "d");
        const fs::path c = root / "d" / "esp_config.json";
        Put(c, "");
        ReadResult r = Read(c, IsJsonObject);
        CHECK(r.status == ReadStatus::Damaged && r.note.find("was empty") != std::string::npos && !fs::exists(c));
        Put(c, std::string("\x00\xff\xfe garbage", 12));
        Put(c.string() + ".bak", "also broken");
        r = Read(c, IsJsonObject);
        CHECK(r.status == ReadStatus::Damaged && r.text.empty() && r.note.find("no usable backup") != std::string::npos);
        Put(c, "[1,2,3]"); // valid JSON, wrong kind for a settings file
        CHECK(Read(c, IsJsonStringArray).status == ReadStatus::Ok); // the same text is fine for a caller that expects an array
        CHECK(Read(c, IsJsonObject).status == ReadStatus::Damaged);
    }
    // ---- only the newest kKeepCorrupt quarantined files stay ----
    {
        fs::remove_all(root / "q");
        const fs::path c = root / "q" / "esp_config.json";
        for (int i = 0; i < 6; ++i) { Put(c, "junk" + std::to_string(i)); Read(c, IsJsonObject); }
        CHECK(CountCorrupt(c.parent_path()) == kKeepCorrupt);
    }

    // ---- Settings: save -> damage -> load restores the previous save; unknown keys survive; debounce ----
    {
        const fs::path dir = root / "s";
        paths::ConfigDirOverride() = dir;
        Settings s;
        s.boxStyleIdx = 3;
        s.unknownKeys["Mine"] = "[1,2]";
        CHECK(s.Save());
        s.boxStyleIdx = 0;
        CHECK(s.Save());
        Put(dir / "esp_config.json", "{\"Version\":1,\"BoxS");
        Settings t;
        t.Load();
        CHECK(t.boxStyleIdx == 3 && t.unknownKeys.count("Mine") == 1);
        CHECK(t.loadNotes.size() == 1 && t.loadNotes[0].find("restored") != std::string::npos);
        Settings u; // garbage and no backup
        fs::remove(dir / "esp_config.json.bak");
        Put(dir / "esp_config.json", "garbage");
        u.Load();
        CHECK(u.boxStyleIdx == Settings{}.boxStyleIdx && u.loadNotes.size() == 1);

        Settings d;
        const auto t0 = std::chrono::steady_clock::now();
        CHECK(!d.SavePending() && !d.SaveIfDue(t0));
        d.RequestSave();
        CHECK(d.SavePending() && !d.SaveIfDue(t0 + 100ms));      // still changing
        d.RequestSave();
        CHECK(!d.SaveIfDue(std::chrono::steady_clock::now() + 100ms));
        CHECK(d.SaveIfDue(std::chrono::steady_clock::now() + 600ms) && !d.SavePending()); // quiet long enough: one write
        d.RequestSave();
        CHECK(d.SavePending() && d.Flush() && !d.SavePending());
        paths::ConfigDirOverride().clear();
    }

    // ---- friends.json: damaged file falls back to its backup ----
    {
        paths::ConfigDirOverride() = root / "fr";
        CHECK(FriendList::Add("Alice") && FriendList::Add("Bob")); // .bak = [Alice]
        Put(root / "fr" / "friends.json", "[\"Alice\",");
        FriendList::Load();
        CHECK(FriendList::Contains("Alice"));
        paths::ConfigDirOverride().clear();
    }

    fs::remove_all(root);
    if (fails) std::printf("%d failure(s)\n", fails); else std::puts("config_store_test: all passed");
    return fails ? 1 : 0;
}
