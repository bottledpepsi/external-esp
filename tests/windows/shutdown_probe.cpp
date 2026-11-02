// shutdown_probe - the console handler of main.cpp and the shutdown sequence, without the overlay. Run it in a console and press Ctrl+C
// (or close the console window): it prints why it is leaving, runs the steps in the fixed order and exits; a second Ctrl+C during the slow
// step ends it at once (exit code 130).
// Build:  x86_64-w64-mingw32-g++ -std=c++20 -I../../src shutdown_probe.cpp ../../src/Shutdown.cpp -o shutdown_probe.exe
#include "Shutdown.h"
#include <windows.h>
#include <cstdio>
#include <string>
#include <thread>

static BOOL WINAPI ConsoleCtrl(DWORD type)
{
    using exitflow::Reason;
    switch (type)
    {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
        if (!exitflow::Global().Request(Reason::CtrlC)) ExitProcess(130);
        return TRUE;
    case CTRL_CLOSE_EVENT:
        exitflow::Global().Request(Reason::ConsoleClosed);
        exitflow::Global().WaitDone(std::chrono::milliseconds(4500));
        return TRUE;
    default: return FALSE;
    }
}

int main(int argc, char** argv)
{
    SetConsoleCtrlHandler(ConsoleCtrl, TRUE);
    // --self[2]: raise Ctrl+C by ourselves after 1 s (twice, 0.5 s apart, with --self2) - for a run without a keyboard.
    // --call[2]: the same, but call the handler function directly from a thread (what Windows does) - for environments (Wine without a console)
    // where GenerateConsoleCtrlEvent is not delivered
    std::thread self;
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode.rfind("--self", 0) == 0 || mode.rfind("--call", 0) == 0)
    {
        const bool twice = mode == "--self2" || mode == "--call2", direct = mode.rfind("--call", 0) == 0;
        self = std::thread([twice, direct]
        {
            auto fire = [direct] { if (direct) ConsoleCtrl(CTRL_C_EVENT); else GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0); };
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            fire();
            if (twice)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(500)); // the slow step is running
                fire();
            }
        });
        self.detach();
    }
    std::printf("running (press Ctrl+C)\n");
    std::fflush(stdout);
    while (!exitflow::Global().Requested()) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::printf("exiting: %s\n", exitflow::Name(exitflow::Global().Why()));
    exitflow::Sequence s;
    s.Add("timer", [] { std::printf("  step timer\n"); });
    s.Add("agent", [] { std::printf("  step agent (slow)\n"); std::fflush(stdout); std::this_thread::sleep_for(std::chrono::milliseconds(1500)); }, std::chrono::milliseconds(2000));
    s.Add("hotkeys", [] { std::printf("  step hotkeys\n"); });
    std::printf("%s\n", exitflow::Sequence::Summary(s.Run()).c_str());
    exitflow::Global().MarkDone();
    return 0;
}
