#include "LegacyImport.h"
#include "Logger.h"

namespace legacy
{
    namespace fs = std::filesystem;

    namespace
    {
        bool CopyOne(const fs::path& from, const fs::path& to, Result& r)
        {
            std::error_code ec;
            if (!fs::is_regular_file(from, ec)) return false;
            fs::create_directories(to.parent_path(), ec);
            ec.clear();
            if (fs::exists(to, ec)) return false; // never overwrite
            fs::copy_file(from, to, fs::copy_options::none, ec);
            if (ec)
            {
                ++r.failed;
                r.notes.push_back("could not copy " + from.string() + ": " + ec.message());
                return false;
            }
            ++r.copied;
            r.notes.push_back("copied " + from.filename().string());
            return true;
        }
    } // namespace

    Result ImportFromExeFolder(const fs::path& exeDir, const fs::path& configDir)
    {
        Result r;
        std::error_code ec;
        if (exeDir.empty() || configDir.empty()) return r;
        if (fs::exists(configDir, ec) && fs::equivalent(exeDir, configDir, ec)) return r; // the same folder (host tests)
        ec.clear();
        // "new folder has been used": any of the three files, the profiles folder, or our marker
        if (fs::exists(configDir / kMarkerFile, ec) || fs::exists(configDir / "esp_config.json", ec) || fs::exists(configDir / "friends.json", ec) ||
            fs::exists(configDir / "profiles", ec))
            return r;

        r.ran = true;
        CopyOne(exeDir / "esp_config.json", configDir / "esp_config.json", r);
        CopyOne(exeDir / "friends.json", configDir / "friends.json", r);
        ec.clear();
        if (fs::is_directory(exeDir / "profiles", ec))
            for (const auto& e : fs::directory_iterator(exeDir / "profiles", ec))
                if (e.is_regular_file(ec) && e.path().extension() == ".json") CopyOne(e.path(), configDir / "profiles" / e.path().filename(), r);

        // the marker is written even when there was nothing to copy: the decision "start from defaults" is also final
        fs::create_directories(configDir, ec);
        ec.clear();
        if (FILE* f = std::fopen((configDir / kMarkerFile).string().c_str(), "wb"))
        {
            std::fputs("The settings of an older build (next to the exe) were considered once. Delete this file to allow another import.\n", f);
            std::fclose(f);
        }
        for (const std::string& n : r.notes) LOG_INFO("Config import: %s", n.c_str());
        if (r.copied > 0) LOG_INFO("Config import: %d file(s) copied from %s to %s (the originals were left in place).", r.copied, exeDir.string().c_str(), configDir.string().c_str());
        return r;
    }
} // namespace legacy
