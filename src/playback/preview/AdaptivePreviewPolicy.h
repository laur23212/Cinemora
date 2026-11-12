#pragma once

#include <cstdint>

namespace drift {

// When the automatic preview quality steps down and back up. No Qt, so it is tested on its own.
//
// Three levels only: Full (1.0), Half (0.5), Quarter (0.25). Late frames lasting more than
// kStepDownAfterMs step one level down; kStepUpAfterMs without a late frame step one level up.
// Every step restarts both timers, so levels change at most one at a time and never rattle.
class AdaptivePreviewPolicy
{
public:
    static constexpr int kLevelCount = 3;
    static constexpr std::int64_t kStepDownAfterMs = 2000;
    static constexpr std::int64_t kStepUpAfterMs = 10000;
    // A late stretch is over once this long passes with only on-time frames.
    static constexpr std::int64_t kLateGapMs = 500;

    static double scaleForLevel(int level)
    {
        return level <= 0 ? 1.0 : (level == 1 ? 0.5 : 0.25);
    }

    int level() const { return m_level; }
    double scale() const { return scaleForLevel(m_level); }

    void reset()
    {
        m_level = 0;
        m_lateSinceMs = -1;
        m_lastLateMs = -1;
        m_smoothSinceMs = -1;
    }

    // A fresh run of playback: forget the running timers but keep the level.
    void restartTimers(std::int64_t nowMs)
    {
        m_lateSinceMs = -1;
        m_lastLateMs = -1;
        m_smoothSinceMs = nowMs;
    }

    // One finished playback frame. Returns true when the level changed.
    bool note(bool late, std::int64_t nowMs)
    {
        if (m_smoothSinceMs < 0)
            m_smoothSinceMs = nowMs;
        if (late) {
            if (m_lateSinceMs < 0 || nowMs - m_lastLateMs > kLateGapMs)
                m_lateSinceMs = nowMs;
            m_lastLateMs = nowMs;
            m_smoothSinceMs = nowMs;
            if (nowMs - m_lateSinceMs > kStepDownAfterMs && m_level < kLevelCount - 1) {
                ++m_level;
                m_lateSinceMs = -1;
                return true;
            }
            return false;
        }
        if (m_lastLateMs >= 0 && nowMs - m_lastLateMs > kLateGapMs)
            m_lateSinceMs = -1;
        if (m_level > 0 && nowMs - m_smoothSinceMs >= kStepUpAfterMs) {
            --m_level;
            m_smoothSinceMs = nowMs;
            return true;
        }
        return false;
    }

private:
    int m_level = 0;
    std::int64_t m_lateSinceMs = -1;
    std::int64_t m_lastLateMs = -1;
    std::int64_t m_smoothSinceMs = -1;
};

} // namespace drift
