#include "ConfigStore.h"
#include "Json.h"
#include "Logger.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>
#ifdef _WIN32
#include "Platform.h"
#else
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#endif

namespace configstore
{
    namespace fs = std::filesystem;

    namespace
    {
        std::function<bool(Stage)>& Hook()
        {
            static std::function<bool(Stage)> h;
            return h;
        }

        bool Alive(Stage s) { return !Hook() || Hook()(s); }

        std::string ReadAll(const fs::path& p, bool* ok = nullptr)
        {
            std::ifstream in(p, std::ios::binary);
            if (ok) *ok = static_cast<bool>(in);
            std::stringstream ss;
            ss << in.rdbuf();
            return ss.str();
        }

        // data -> file, flushed to the disk before it returns
        bool WriteDurable(const fs::path& p, const std::string& data, std::string& err)
        {
#ifdef _WIN32
            HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE)
            {
                err = "cannot create " + p.filename().string() + " (Windows error " + std::to_string(GetLastError()) + ")";
                return false;
            }
            bool ok = true;
            size_t off = 0;
            while (ok && off < data.size())
            {
                DWORD n = 0;
                ok = WriteFile(h, data.data() + off, static_cast<DWORD>(std::min<size_t>(data.size() - off, 1u << 20)), &n, nullptr) && n > 0;
                off += n;
            }
            if (ok) ok = FlushFileBuffers(h) != 0;
            const DWORD last = ok ? 0 : GetLastError();
            CloseHandle(h);
            if (!ok) err = "cannot write " + p.filename().string() + " (Windows error " + std::to_string(last) + ")";
            return ok;
#else
            const int fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0)
            {
                err = "cannot create " + p.filename().string() + " (" + std::strerror(errno) + ")";
                return false;
            }
            bool ok = true;
            size_t off = 0;
            while (ok && off < data.size())
            {
                const ssize_t n = ::write(fd, data.data() + off, data.size() - off);
                if (n <= 0) ok = false;
                else off += static_cast<size_t>(n);
            }
            if (ok) ok = ::fsync(fd) == 0;
            const int e = errno;
            ::close(fd);
            if (!ok) err = "cannot write " + p.filename().string() + " (" + std::strerror(e) + ")";
            return ok;
#endif
        }

        // from -> to, replacing `to`, in one step. A virus scanner or the search indexer can hold the target for a moment: retry briefly.
        bool Replace(const fs::path& from, const fs::path& to, std::string& err)
        {
#ifdef _WIN32
            DWORD last = 0;
            for (int attempt = 0; attempt < 5; ++attempt)
            {
                if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
                last = GetLastError();
                if (last != ERROR_ACCESS_DENIED && last != ERROR_SHARING_VIOLATION && last != ERROR_LOCK_VIOLATION) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(25 * (attempt + 1)));
            }
            err = "cannot replace " + to.filename().string() + " (Windows error " + std::to_string(last) + ")";
            return false;
#else
            std::error_code ec;
            fs::rename(from, to, ec);
            if (ec) err = "cannot replace " + to.filename().string() + " (" + ec.message() + ")";
            return !ec;
#endif
        }

        fs::path With(const fs::path& p, const char* suffix)
        {
            fs::path q = p;
            q += suffix;
            return q;
        }

        std::string Stamp()
        {
            const std::time_t t = std::time(nullptr);
            char b[32];
            std::strftime(b, sizeof b, "%Y%m%d-%H%M%S", std::localtime(&t));
            return b;
        }

        // <stem>.corrupt-<stamp>[-n]<ext>, then only the newest kKeepCorrupt are kept
        fs::path Quarantine(const fs::path& file)
        {
            const fs::path dir = file.parent_path();
            const std::string stem = file.stem().string() + ".corrupt-", ext = file.extension().string();
            fs::path dest;
            for (int n = 0; n < 1000; ++n)
            {
                dest = dir / (stem + Stamp() + (n ? "-" + std::to_string(n) : std::string()) + ext);
                std::error_code ec;
                if (!fs::exists(dest, ec)) break;
            }
            std::error_code ec;
            fs::rename(file, dest, ec);
            if (ec)
            {
                LOG_WARN("ConfigStore: could not keep the damaged %s as %s: %s", file.filename().string().c_str(), dest.filename().string().c_str(), ec.message().c_str());
                return {};
            }
            std::vector<fs::path> old;
            for (const auto& e : fs::directory_iterator(dir, ec))
            {
                const std::string n = e.path().filename().string();
                if (n.rfind(stem, 0) == 0 && e.path().extension() == ext) old.push_back(e.path());
            }
            std::sort(old.begin(), old.end());
            while (static_cast<int>(old.size()) > kKeepCorrupt)
            {
                fs::remove(old.front(), ec);
                old.erase(old.begin());
            }
            return dest;
        }
    } // namespace

    void SetFaultHook(std::function<bool(Stage)> hook) { Hook() = std::move(hook); }

    bool IsJsonObject(const std::string& text) { return json::ParseObject(text).has_value() && json::IsValid(text); }
    bool IsJsonStringArray(const std::string& text) { return json::ParseStringArray(text).has_value() && json::IsValid(text); }

    bool WriteAtomic(const fs::path& target, const std::string& data, std::string* error, const WriteOptions& opt)
    {
        std::string err;
        std::error_code ec;
        fs::create_directories(target.parent_path(), ec);
        const fs::path tmp = With(target, ".tmp"), bak = With(target, ".bak");

        if (!WriteDurable(tmp, data, err))
        {
            fs::remove(tmp, ec);
            if (error) *error = err;
            return false;
        }
        if (!Alive(Stage::TempWritten))
        {
            if (error) *error = "injected stop after the temporary file";
            return false;
        }
        if (opt.keepBackup && fs::exists(target, ec))
        {
            // the last GOOD file: a damaged one must not replace a good backup
            const std::string prev = ReadAll(target);
            const bool good = opt.validPrevious ? opt.validPrevious(prev) : json::IsValid(prev);
            if (good)
            {
                const fs::path bakTmp = With(target, ".bak.tmp");
                std::string e2;
                if (WriteDurable(bakTmp, prev, e2) && Replace(bakTmp, bak, e2)) { /* made */ }
                else
                {
                    fs::remove(bakTmp, ec);
                    LOG_WARN("ConfigStore: could not refresh %s: %s", bak.filename().string().c_str(), e2.c_str()); // not fatal: the new file is still written
                }
            }
        }
        if (!Alive(Stage::BackupMade))
        {
            if (error) *error = "injected stop after the backup";
            return false;
        }
        if (!Replace(tmp, target, err))
        {
            fs::remove(tmp, ec);
            if (error) *error = err;
            return false;
        }
        return true;
    }

    ReadResult Read(const fs::path& file, const Validator& valid)
    {
        ReadResult r;
        std::error_code ec;
        fs::remove(With(file, ".tmp"), ec); // left by a write that was interrupted; never complete, never needed
        fs::remove(With(file, ".bak.tmp"), ec);
        if (!fs::exists(file, ec)) return r; // Missing
        bool opened = false;
        const std::string text = ReadAll(file, &opened);
        if (!opened)
        {
            r.status = ReadStatus::Unreadable;
            r.note = file.filename().string() + " could not be opened (is another program using it?) - using the defaults for now, and not touching the file";
            return r;
        }
        if (valid(text))
        {
            r.status = ReadStatus::Ok;
            r.text = text;
            return r;
        }

        // damaged: keep it for inspection, then try the last good copy
        const bool wasEmpty = text.find_first_not_of(" \t\r\n") == std::string::npos;
        r.quarantined = Quarantine(file);
        const std::string what = file.filename().string() + (wasEmpty ? " was empty" : " was damaged (not valid JSON)");
        const std::string kept = r.quarantined.empty() ? std::string() : "; kept as " + r.quarantined.filename().string();
        const fs::path bak = With(file, ".bak");
        if (fs::exists(bak, ec))
        {
            const std::string b = ReadAll(bak);
            if (valid(b))
            {
                std::string err;
                WriteOptions o;
                o.keepBackup = false;
                if (!WriteAtomic(file, b, &err, o)) LOG_WARN("ConfigStore: restored %s in memory only: %s", file.filename().string().c_str(), err.c_str());
                r.status = ReadStatus::RestoredFromBackup;
                r.text = b;
                r.note = what + kept + "; the last good copy was restored";
                return r;
            }
        }
        r.status = ReadStatus::Damaged;
        r.note = what + kept + "; there is no usable backup, so the defaults are used";
        return r;
    }
} // namespace configstore
