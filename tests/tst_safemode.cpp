// Plain C++ (no Qt): the rule that puts a start in safe mode.
#include "core/base/SafeMode.h"

#include <cstdio>

using namespace drift::safemode;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main()
{
    CHECK(!shouldEnter(0, false));          // normal start
    CHECK(!shouldEnter(1, false));          // one failed start is not enough
    CHECK(shouldEnter(2, false));           // two in a row
    CHECK(shouldEnter(5, false));           // and it stays safe until a start succeeds
    CHECK(shouldEnter(0, true));            // --safe-mode
    CHECK(!active());
    setActive(true);
    CHECK(active());
    setActive(false);
    CHECK(!active());

    // The count the launcher keeps: +1 at start, back to 0 at the first frame.
    int stored = 0;
    auto launch = [&](bool crash) { bool safe = shouldEnter(stored, false); ++stored; if (!crash) stored = 0; return safe; };
    CHECK(!launch(true));   // crash 1
    CHECK(!launch(true));   // crash 2
    CHECK(launch(false));   // third start is safe and succeeds
    CHECK(!launch(false));  // next one is normal again
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures ? 1 : 0;
}
