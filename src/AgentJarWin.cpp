// AgentJarWin.cpp - finds the jar inside this exe's resource section and puts it where the attach helper can read it (Windows only; the rules live
// in AgentJar.cpp). The resource is added by CMake / build-all.bat from agent\esp-agent.jar:  ESPAGENT RCDATA "esp-agent.jar".
#include "AgentJarWin.h"
#include "Logger.h"
#include "Paths.h"
#include "Platform.h"

namespace agentjar
{
    bool EmbeddedBlob(Blob& out)
    {
        HRSRC r = FindResourceW(nullptr, L"ESPAGENT", reinterpret_cast<LPCWSTR>(RT_RCDATA));
        if (!r) return false;
        HGLOBAL g = LoadResource(nullptr, r);
        const DWORD n = SizeofResource(nullptr, r);
        if (!g || n == 0) return false;
        out.data = static_cast<const unsigned char*>(LockResource(g));
        out.size = n;
        return out.Valid();
    }

    std::filesystem::path CacheDir() { return paths::UserFolder(L"LOCALAPPDATA", AppDir()) / "agent"; }

    Resolved Resolve(const std::filesystem::path& explicitJar, const std::filesystem::path& besideExe)
    {
        Resolved r;
        r.exportDir = AppDir(); // esp_agent.log, esp_selftest.txt: next to the exe, as before
        if (!explicitJar.empty())
        {
            r.jar = explicitJar;
            r.source = Resolved::Source::CommandLine;
            r.exportDir = explicitJar.parent_path();
            return r;
        }
        Blob b;
        if (EmbeddedBlob(b))
        {
            const Extracted e = EnsureExtracted(b, CacheDir());
            if (e.how != Extracted::How::Failed)
            {
                r.jar = e.path;
                r.source = Resolved::Source::Embedded;
                r.hash = e.hash;
                r.how = e.how;
                const Cleaned c = CleanOld(CacheDir(), e.path.filename().string());
                r.removedOld = c.removed;
                return r;
            }
            LOG_ERROR("Agent jar: %s", e.error.c_str());
            r.problem = e.error;
        }
        r.jar = besideExe;
        r.source = Resolved::Source::BesideExe;
        return r;
    }
} // namespace agentjar
