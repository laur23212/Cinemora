#pragma once

#include <atomic>

// Safe mode: a start with the parts most likely to crash it switched off. No Qt, so the rule is
// tested on its own and any layer can ask. Set once at startup (app/StartupGuard.cpp) and never
// changed after; nothing here writes a user setting, so the next normal start is untouched.
namespace drift::safemode {

// Settings keys (QSettings): launches that have started without reaching the first frame.
inline constexpr const char *kAttemptsKey = "startup/unfinishedLaunches";
// Crashed starts in a row that put the next one in safe mode.
inline constexpr int kCrashesBeforeSafeMode = 2;

// `unfinishedLaunches` is how many earlier launches never reached the first frame.
inline bool shouldEnter(int unfinishedLaunches, bool flagGiven)
{
    return flagGiven || unfinishedLaunches >= kCrashesBeforeSafeMode;
}

inline std::atomic<bool> &flag()
{
    static std::atomic<bool> value{false};
    return value;
}

inline bool active() { return flag().load(std::memory_order_relaxed); }
inline void setActive(bool on) { flag().store(on, std::memory_order_relaxed); }

} // namespace drift::safemode
