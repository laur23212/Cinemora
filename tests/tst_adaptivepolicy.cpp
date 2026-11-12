// Plain C++ (no Qt) so it also runs where Qt is not installed.
#include "playback/preview/AdaptivePreviewPolicy.h"

#include <cstdio>
#include <cstdlib>

using drift::AdaptivePreviewPolicy;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

// Feeds one frame every 33 ms, late or not, from `from` to `to`; returns the last time used.
static long feed(AdaptivePreviewPolicy &p, long from, long to, bool late)
{
    long t = from;
    for (; t <= to; t += 33)
        p.note(late, t);
    return t;
}

int main()
{
    {   // Under 2 s of late frames changes nothing; over 2 s steps down exactly one level.
        AdaptivePreviewPolicy p;
        feed(p, 0, 1900, true);
        CHECK(p.level() == 0);
        feed(p, 1933, 2100, true);
        CHECK(p.level() == 1 && p.scale() == 0.5);
    }
    {   // Keeps going Half -> Quarter after another 2 s, and stops at Quarter.
        AdaptivePreviewPolicy p;
        feed(p, 0, 10000, true);
        CHECK(p.level() == 2 && p.scale() == 0.25);
    }
    {   // A single stutter every second never adds up to a step.
        AdaptivePreviewPolicy p;
        for (long t = 0; t < 20000; t += 33)
            p.note(t % 1000 < 33, t);
        CHECK(p.level() == 0);
    }
    {   // Steps up only after 10 s without a late frame, one level at a time.
        AdaptivePreviewPolicy p;
        long t = feed(p, 0, 10000, true);
        CHECK(p.level() == 2);
        t = feed(p, t, t + 9000, false);
        CHECK(p.level() == 2);
        t = feed(p, t, t + 1500, false);
        CHECK(p.level() == 1);
        t = feed(p, t, t + 9000, false);
        CHECK(p.level() == 1);
        feed(p, t, t + 1500, false);
        CHECK(p.level() == 0);
    }
    {   // A late frame during recovery restarts the 10 s.
        AdaptivePreviewPolicy p;
        long t = feed(p, 0, 2100, true);
        CHECK(p.level() == 1);
        t = feed(p, t, t + 8000, false);
        p.note(true, t);
        t = feed(p, t + 33, t + 9000, false);
        CHECK(p.level() == 1);
        feed(p, t, t + 1500, false);
        CHECK(p.level() == 0);
    }
    {   // reset() returns to Full.
        AdaptivePreviewPolicy p;
        feed(p, 0, 2100, true);
        p.reset();
        CHECK(p.level() == 0 && p.scale() == 1.0);
    }
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures ? 1 : 0;
}
