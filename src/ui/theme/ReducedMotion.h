#pragma once

// Dafin: the "reduce motion" switch for the interface.
//
// Header-only on purpose, so ThemeManager.cpp (also compiled into several test targets) needs no
// new source file. main.cpp asks the operating system once at start (see
// app/platform/ReducedMotion.cpp) and calls setReduced(); ThemeManager::apply() then runs the
// theme's motion tokens through reduceTokens(), so every Behavior and animation that takes its
// duration from Theme.durationFast / Base / Slow / Press finishes at once and the press-scale
// "squash" is off. Looping indicators (spinners, the recording dot) are not touched.

#include <atomic>

#include <QString>
#include <QVariant>
#include <QVariantMap>

namespace drift::motion {

inline std::atomic<bool> &reducedFlag()
{
    static std::atomic<bool> flag{false};
    return flag;
}

inline bool reduced() { return reducedFlag().load(std::memory_order_relaxed); }
inline void setReduced(bool on) { reducedFlag().store(on, std::memory_order_relaxed); }

// `motion` is the theme's "motion" group. Does nothing unless reduced motion is on.
inline void reduceTokens(QVariantMap &motion)
{
    if (!reduced())
        return;
    for (const char *key : {"durationFast", "durationBase", "durationSlow", "durationPress"}) {
        const QString name = QLatin1String(key);
        if (motion.contains(name))
            motion.insert(name, 0);
    }
    if (motion.contains(QStringLiteral("pressScale")))
        motion.insert(QStringLiteral("pressScale"), 1.0);
}

} // namespace drift::motion
