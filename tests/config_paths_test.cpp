// config_paths_test - the config folder constant, the Settings/friends/profiles stores writing into it, and the one-time import of the
// files an older build kept next to the exe. Portable: the config folder is redirected into the test's own directory tree.
#include "EspConfig.h"
#include "FriendList.h"
#include "LegacyImport.h"
#include "Paths.h"
#include "ProfileManager.h"
#include <cstdio>
#include <fstream>
#include <sstream>
namespace fs = std::filesystem;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

static void Write(const fs::path& p, const std::string& text)
{
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}
static std::string Read(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int main()
{
    // the folder name is one constant
    CHECK(std::string(paths::kAppFolderName) == "external-esp");

    const fs::path root = fs::current_path() / "cfgtest";
    fs::remove_all(root);
    const fs::path exeDir = root / "exe", cfg = root / "appdata" / paths::kAppFolderName;
    fs::create_directories(exeDir);

    // ---- import: copies the three kinds of file, leaves the originals, writes the marker ----
    Write(exeDir / "esp_config.json", "{\"BoxStyle\":\"Skeleton\"}");
    Write(exeDir / "friends.json", "[\"Alice\"]");
    Write(exeDir / "profiles" / "pvp.json", "{\"Fov\":90}");
    Write(exeDir / "profiles" / "notes.txt", "not a profile");
    auto r = legacy::ImportFromExeFolder(exeDir, cfg);
    CHECK(r.ran && r.copied == 3 && r.failed == 0);
    CHECK(Read(cfg / "esp_config.json") == "{\"BoxStyle\":\"Skeleton\"}");
    CHECK(Read(cfg / "friends.json") == "[\"Alice\"]");
    CHECK(Read(cfg / "profiles" / "pvp.json") == "{\"Fov\":90}");
    CHECK(!fs::exists(cfg / "profiles" / "notes.txt"));
    CHECK(fs::exists(exeDir / "esp_config.json") && fs::exists(exeDir / "friends.json")); // originals stay
    CHECK(fs::exists(cfg / legacy::kMarkerFile));

    // ---- runs once: changing the new copy and importing again must not overwrite it, ... ----
    Write(cfg / "esp_config.json", "{\"BoxStyle\":\"Sharp\"}");
    r = legacy::ImportFromExeFolder(exeDir, cfg);
    CHECK(!r.ran && r.copied == 0);
    CHECK(Read(cfg / "esp_config.json") == "{\"BoxStyle\":\"Sharp\"}");
    // ... and deleting the config to start over must not resurrect the old one
    fs::remove(cfg / "esp_config.json");
    fs::remove(cfg / "friends.json");
    fs::remove_all(cfg / "profiles");
    r = legacy::ImportFromExeFolder(exeDir, cfg);
    CHECK(!r.ran && !fs::exists(cfg / "esp_config.json"));

    // ---- nothing to import: still decided (marker), nothing created but the marker ----
    const fs::path empty = root / "exe2", cfg2 = root / "appdata2" / paths::kAppFolderName;
    fs::create_directories(empty);
    r = legacy::ImportFromExeFolder(empty, cfg2);
    CHECK(r.ran && r.copied == 0 && fs::exists(cfg2 / legacy::kMarkerFile));
    CHECK(!fs::exists(cfg2 / "esp_config.json"));

    // ---- an already used new folder is never touched ----
    const fs::path used = root / "appdata3" / paths::kAppFolderName;
    Write(used / "esp_config.json", "{}");
    r = legacy::ImportFromExeFolder(exeDir, used);
    CHECK(!r.ran && !fs::exists(used / "friends.json") && !fs::exists(used / legacy::kMarkerFile));

    // ---- the same folder (the host tests run with exe dir == config dir): no-op ----
    r = legacy::ImportFromExeFolder(exeDir, exeDir);
    CHECK(!r.ran);

    // ---- the stores write into ConfigDir(), creating it ----
    const fs::path home = root / "appdata4" / paths::kAppFolderName;
    paths::ConfigDirOverride() = home;
    CHECK(ConfigDir() == home);
    CHECK(Settings::ConfigPath() == home / "esp_config.json");
    CHECK(!fs::exists(home));
    Settings s;
    s.boxStyleIdx = 3;
    s.Save();
    CHECK(fs::exists(home / "esp_config.json"));
    Settings back;
    back.Load();
    CHECK(back.boxStyleIdx == 3);
    CHECK(FriendList::Add("Bob"));
    CHECK(fs::exists(home / "friends.json"));
    ProfileManager::Refresh();
    CHECK(ProfileManager::Save("one", s.Build()));
    CHECK(fs::exists(home / "profiles" / "one.json"));
    paths::ConfigDirOverride().clear();

    fs::remove_all(root);
    if (fails) std::printf("%d failure(s)\n", fails);
    else std::puts("config_paths_test: all passed");
    return fails ? 1 : 0;
}
