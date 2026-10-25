#include "AgentInjector.h"
#include "Wire.h"
#include "Logger.h"
#include <chrono>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    // True if the runtime is JDK 9+ (needs --add-modules jdk.attach); false for 1.8 (needs tools.jar).
    // Looks at the "release" file one level above bin\, like the C# tool. Unknown -> assume modern.
    bool IsJdk9Plus(const fs::path& javaExe)
    {
        std::ifstream f(javaExe.parent_path().parent_path() / "release");
        for (std::string line; std::getline(f, line);)
        {
            if (line.rfind("JAVA_VERSION=", 0) == 0)
            {
                std::string v = line.substr(13);
                v.erase(std::remove(v.begin(), v.end(), '"'), v.end());
                return v.rfind("1.", 0) != 0;
            }
        }
        return true;
    }

    fs::path FindToolsJar(const fs::path& javaExe)
    {
        std::error_code ec;
        fs::path t = javaExe.parent_path().parent_path() / "lib" / "tools.jar";
        if (fs::exists(t, ec)) return t;
        t = javaExe.parent_path().parent_path().parent_path() / "lib" / "tools.jar"; // jre\bin -> jdk\lib
        return fs::exists(t, ec) ? t : fs::path{};
    }

    std::wstring Q(const std::wstring& s) { return L"\"" + s + L"\""; }
} // namespace

void AgentInjector::Set(State s, const std::string& msg, int progress)
{
    {
        std::lock_guard<std::mutex> l(mtx_);
        msg_ = msg;
    }
    progress_ = progress;
    state_ = s;
    LOG_INFO("AgentInjector: [%d%%] %s", progress, msg.c_str());
}

void AgentInjector::Reset()
{
    if (state_ == State::Running) return;
    Join();
    Set(State::Idle, "", 0);
}

void AgentInjector::Start(const JavaProcess& target)
{
    if (state_ == State::Running) return;
    Join(); // previous (finished) worker
    state_ = State::Running;
    thread_ = std::jthread([this, target](std::stop_token st) { Run(target, st); });
}

// Runs the helper JVM, relays its output, waits (abortable). Returns the exit code, or -1 if it could not start.
int AgentInjector::RunHelper(const fs::path& java, DWORD pid, std::stop_token st, std::string& output)
{
    std::wstring classpath = cfg_.jarPath.wstring();
    const bool modern = IsJdk9Plus(java);
    if (!modern)
    {
        fs::path tools = FindToolsJar(java);
        if (!tools.empty()) classpath += L";" + tools.wstring();
    }
    // Agent options: UDP port, send rate, and where to write esp_layout.json / esp_agent.log (next to the exe).
    std::wstring exportDir = cfg_.jarPath.parent_path().wstring();
    for (auto& c : exportDir) if (c == L'\\') c = L'/'; // avoids backslash-before-quote issues
    // wire=N: the newest wire protocol this overlay can read; the agent never sends a newer one (an agent without this option, or an older
    // overlay that does not pass it, means v4).
    const std::wstring opts = L"port=" + std::to_wstring(cfg_.port) + L";hz=" + std::to_wstring(cfg_.agentHz) +
        L";wire=" + std::to_wstring(wire::kMaxVersion) + L";export=" + exportDir;

    std::wstring cmd = Q(java.wstring());
    if (modern) cmd += L" --add-modules jdk.attach";
    cmd += L" -cp " + Q(classpath) + L" mcradar.Attach " + std::to_wstring(pid) + L" " + Q(cfg_.jarPath.wstring()) +
        L" " + Q(opts);

    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    UniqueHandle readEnd(rd), writeEnd(wr);
    SetHandleInformation(readEnd.get(), HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writeEnd.get();
    si.hStdError = writeEnd.get();
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        LOG_ERROR("AgentInjector: could not start %s (error %lu)", java.string().c_str(),
                  static_cast<unsigned long>(GetLastError()));
        return -1;
    }
    UniqueHandle proc(pi.hProcess), thread(pi.hThread);
    writeEnd.reset(); // otherwise the pipe never reports EOF

    std::string pending;
    auto drain = [&]()
    {
        for (;;)
        {
            DWORD avail = 0;
            if (!PeekNamedPipe(readEnd.get(), nullptr, 0, nullptr, &avail, nullptr) || avail == 0) break;
            char b[512];
            DWORD got = 0;
            if (!ReadFile(readEnd.get(), b, sizeof b, &got, nullptr) || got == 0) break;
            pending.append(b, got);
        }
        size_t nl;
        while ((nl = pending.find('\n')) != std::string::npos)
        {
            std::string line = pending.substr(0, nl);
            pending.erase(0, nl + 1);
            while (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            output += line + "\n";
            LOG_INFO("AttachHelper: %s", line.c_str());
        }
    };

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
    for (;;)
    {
        DWORD w = WaitForSingleObject(proc.get(), 100);
        drain();
        if (w == WAIT_OBJECT_0) break;
        if (st.stop_requested() || std::chrono::steady_clock::now() > deadline)
        {
            TerminateProcess(proc.get(), 1);
            output += "attach helper timed out / aborted\n";
            return -1;
        }
    }
    drain();
    if (!pending.empty()) output += pending + "\n";
    DWORD code = 1;
    GetExitCodeProcess(proc.get(), &code);
    return static_cast<int>(code);
}

void AgentInjector::Run(JavaProcess target, std::stop_token st)
{
    try
    {
        Set(State::Running, "preparing...", 10);
        std::error_code ec;
        if (!fs::exists(cfg_.jarPath, ec))
        {
            Set(State::Failed, "agent jar not found: " + cfg_.jarPath.string() + " (build it with agent\\build.bat)",
                0);
            return;
        }

        // Candidate JVMs that will run the attach helper.
        std::vector<fs::path> cands;
        auto add = [&](const fs::path& p)
        {
            std::error_code e;
            if (p.empty() || !fs::exists(p, e)) return;
            for (const auto& c : cands) if (fs::equivalent(c, p, e)) return;
            cands.push_back(p);
        };
        add(target.javaExe);
        if (!cfg_.javaPath.empty()) add(cfg_.javaPath);
        wchar_t home[MAX_PATH] = {};
        DWORD n = GetEnvironmentVariableW(L"JAVA_HOME", home, MAX_PATH);
        if (n > 0 && n < MAX_PATH) add(fs::path(home) / "bin" / "java.exe");
        wchar_t onPath[MAX_PATH] = {};
        if (SearchPathW(nullptr, L"java.exe", nullptr, MAX_PATH, onPath, nullptr) > 0) add(onPath);
        if (cands.empty())
        {
            Set(State::Failed, "no java.exe found (install a JDK 17+ or pass --java <path>)", 0);
            return;
        }

        std::string lastErr;
        int i = 0;
        for (const auto& java : cands)
        {
            if (st.stop_requested())
            {
                Set(State::Failed, "cancelled", 0);
                return;
            }
            Set(State::Running, "attaching via " + java.parent_path().parent_path().filename().string() + " ...",
                30 + 15 * i++);
            std::string out;
            int code = RunHelper(java, target.pid, st, out);
            if (code == 0)
            {
                Set(State::Succeeded, "agent loaded - waiting for first data", 80);
                return;
            }
            // 3 = no Attach API in that Java, "Module jdk.attach not found" = jlinked runtime without it: try the next JVM.
            lastErr = code == 3
                          ? "that Java has no Attach API (needs a full JDK)"
                          : out.find("jdk.attach") != std::string::npos
                          ? "that Java runtime lacks the jdk.attach module"
                          : out.empty()
                          ? "helper exited with code " + std::to_string(code)
                          : out.substr(0, out.find('\n'));
            LOG_WARN("AgentInjector: candidate %s failed: %s", java.string().c_str(), lastErr.c_str());
        }
        Set(State::Failed, lastErr + (cands.size() == 1 ? " - try --java <path to a JDK 17+ java.exe>" : ""), 0);
    }
    catch (const std::exception& e)
    {
        Set(State::Failed, std::string("unexpected error: ") + e.what(), 0);
    }
}
