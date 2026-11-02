// ConfigStore.h - crash-safe reading and writing of the settings files (esp_config.json, friends.json, profiles\*.json).
//
// WriteAtomic():  data -> <file>.tmp (flushed to the disk) -> [the previous good file -> <file>.bak] -> <file>.tmp replaces <file> in one step.
//                 A crash or power loss at any point leaves either the complete old file or the complete new one; never half of each.
// Read():         a file that is missing, empty, cut off or garbage is not trusted: it is kept as <stem>.corrupt-<time>.<ext> (the newest 3 are
//                 kept), the last good copy (<file>.bak) is restored from if there is one, and the caller is told what happened in plain words.
// Portable: POSIX calls on the host tests, Win32 calls (CreateFile/FlushFileBuffers/MoveFileEx) in the exe.
#pragma once
#include <filesystem>
#include <functional>
#include <string>

namespace configstore
{
    inline constexpr int kKeepCorrupt = 3; // quarantined files kept per settings file

    // Is this text a usable file of the kind the caller expects? (an object for esp_config.json, an array of strings for friends.json)
    using Validator = std::function<bool(const std::string&)>;
    bool IsJsonObject(const std::string& text);
    bool IsJsonStringArray(const std::string& text);

    struct WriteOptions
    {
        bool keepBackup = true; // copy the previous file to <file>.bak first, when it is valid
        Validator validPrevious; // what makes the previous file worth keeping (default: any valid JSON)
    };

    // Returns false and fills `error` (one line, with the Windows/errno reason) when the file could not be written; the old file is then untouched.
    bool WriteAtomic(const std::filesystem::path& target, const std::string& data, std::string* error = nullptr, const WriteOptions& opt = {});

    enum class ReadStatus
    {
        Ok, // the file was read as it is
        Missing, // no file (a first run): the caller uses its defaults
        RestoredFromBackup, // the file was damaged; <file>.bak was valid and has been put back
        Damaged, // the file was damaged and there is no usable backup: the caller uses its defaults
        Unreadable, // the file exists but could not be opened (permissions, locked): it was NOT touched
    };

    struct ReadResult
    {
        ReadStatus status = ReadStatus::Missing;
        std::string text; // the text to load (Ok, RestoredFromBackup); empty otherwise
        std::string note; // one plain sentence for the log / the UI; empty for Ok and Missing
        std::filesystem::path quarantined; // where the damaged file went, when it was moved
    };

    ReadResult Read(const std::filesystem::path& file, const Validator& valid);

    // ---- test hooks: stop WriteAtomic dead (as a crash would, no clean-up) at a stage ----
    enum class Stage { TempWritten, BackupMade };
    void SetFaultHook(std::function<bool(Stage)> hook); // return false = "die here"; an empty function removes the hook
} // namespace configstore
