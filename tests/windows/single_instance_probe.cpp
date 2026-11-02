// single_instance_probe - run two copies (Windows, or Wine) to see the guard:
//   single_instance_probe 47615 hold    -> "first: got it", waits 6 s
//   single_instance_probe 47615         -> "second: already running" (exit 17) while the first waits
//   single_instance_probe 47616         -> a different port is independent: "first: got it"
// Build:  x86_64-w64-mingw32-g++ -std=c++20 -I../../src single_instance_probe.cpp ../../src/SingleInstance.cpp -o single_instance_probe.exe
#include "ExitCodes.h"
#include "SingleInstance.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv)
{
    const uint16_t port = static_cast<uint16_t>(argc > 1 ? std::atoi(argv[1]) : 47615);
    SingleInstance s;
    if (!s.Acquire(port))
    {
        std::printf("second: already running on port %u\n", static_cast<unsigned>(port));
        return exitcode::kAlreadyRunning;
    }
    std::printf("first: got it on port %u\n", static_cast<unsigned>(port));
    std::fflush(stdout);
    if (argc > 2 && !std::strcmp(argv[2], "hold")) Sleep(6000);
    return 0;
}
