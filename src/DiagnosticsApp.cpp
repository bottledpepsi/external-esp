#include "DiagnosticsApp.h"
#include "Diagnostics.h"
#include "FriendList.h"
#include "Logger.h"
#include "Paths.h"
#include "SysInfo.h"
#include <chrono>
#include <ctime>

namespace diag
{
    std::string Collect(const AppContext& ctx)
    {
        const sysinfo::Info sys = sysinfo::Collect();
        Redactor red;
        red.AddName(sys.userName, "<user>");
        red.AddName(sys.hostName, "<host>");
        red.AddPathPrefix(sys.userProfile, "%USERPROFILE%");
        red.AddPathPrefix(AppDir().string(), "<exe dir>");
        if (!ctx.agentLog.empty()) red.AddPathPrefix(ctx.agentLog.parent_path().string(), "<export dir>");
        red.AddSecret(ctx.token);
        red.AddName(ctx.localName, "<local>");
        for (const std::string& n : ctx.playerNames) red.AddName(n);
        for (const std::string& n : FriendList::Names()) red.AddName(n);

        Input in;
        // the log tails are read first so names found in them can be learnt before anything is written out
        const std::filesystem::path espLog = elog::LogFile();
        in.espLog = diag::ReadTail(espLog.string(), kLogTailLines);
        in.espLogBackupTail = diag::ReadTail(elog::BackupFile().string(), kLogTailLines);
        in.agentLog = ctx.agentLog.empty() ? std::string() : diag::ReadTail(ctx.agentLog.string(), kLogTailLines);
        red.HarvestNames(in.espLog);
        red.HarvestNames(in.espLogBackupTail);
        red.HarvestNames(in.agentLog);

        const std::time_t t = std::time(nullptr);
        char ts[32];
        std::strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", std::localtime(&t));
        in.generated = ts;
        in.version = ctx.version.empty() ? "unknown" : ctx.version;
        in.compiler = sysinfo::CompilerName();
        in.os = sys.os;
        in.cpu = sys.cpu;
        in.renderer = ctx.renderer.empty() ? "(overlay not running)" : ctx.renderer;
        in.settings = ctx.settings;
        in.hudStatus = ctx.hudStatus;
        in.streamStats = ctx.streamStats;
        return Build(in, red);
    }
} // namespace diag
