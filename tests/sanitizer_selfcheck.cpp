// Proof that a sanitizer build really sanitizes: each mode contains one deliberate bug that its sanitizer MUST stop. The ctest entries are marked
// WILL_FAIL, so ctest reports "passed" only when the program was stopped - if the sanitizer were silently not active the program would run to the end
// and exit 0, and ctest would fail.
//   sanitizer_selfcheck race | heap | ub
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

static volatile int g_sink = 0;

int main(int argc, char** argv)
{
    const char* mode = argc > 1 ? argv[1] : "";
    if (!std::strcmp(mode, "race"))
    {
        int shared = 0;                                                    // two threads write it with no synchronisation: ThreadSanitizer must report it
        std::thread a([&] { for (int i = 0; i < 100000; ++i) shared = shared + 1; });
        std::thread b([&] { for (int i = 0; i < 100000; ++i) shared = shared + 1; });
        a.join();
        b.join();
        g_sink = shared;
    }
    else if (!std::strcmp(mode, "heap"))
    {
        std::vector<int> v(8);
        g_sink = v.data()[8 + static_cast<int>(argc) - 1];                   // one past the end: AddressSanitizer must report it
    }
    else if (!std::strcmp(mode, "ub"))
    {
        volatile double nan = 0.0;
        nan = nan / nan;
        g_sink = static_cast<int>(nan);                                      // NaN -> int: float-cast-overflow (the bug the wire parser had)
    }
    else
    {
        std::puts("usage: sanitizer_selfcheck race|heap|ub");
        return 2;
    }
    std::puts("the program ran to the end: the sanitizer did NOT stop it");
    return 0;
}
