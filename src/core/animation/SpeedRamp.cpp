#include "core/animation/SpeedRamp.h"

#include "core/edit/TimelineOps.h"
#include "core/project/clip/Clip.h"

#include <QCoreApplication>
#include <QtGlobal>

#include <cmath>

namespace drift {

namespace {

// A smooth ramp through (pos, speed) points: every point gets flat tangents reaching a third of the
// way to its neighbours, so peaks and dips are rounded the way Filmora draws them rather than the
// sharp corners zero-length handles would give.
QList<SpeedPoint> smoothRamp(std::initializer_list<std::pair<double, double>> knots)
{
    QList<SpeedPoint> points;
    for (const auto &[pos, speed] : knots) {
        SpeedPoint point;
        point.pos = pos;
        point.speed = speed;
        points.append(point);
    }
    for (int i = 0; i < points.size(); ++i) {
        SpeedPoint &point = points[i];
        if (i > 0)
            point.inDx = -(point.pos - points.at(i - 1).pos) / 3.0;
        if (i + 1 < points.size())
            point.outDx = (points.at(i + 1).pos - point.pos) / 3.0;
    }
    return points;
}

QString trPreset(const char *text)
{
    return QCoreApplication::translate("SpeedRamp", text);
}

} // namespace

bool clipAudioFollowsSpeed(const Clip &clip)
{
    if (clip.hasSpeedCurve())
        return true; // a curve is already held inside the range the stretcher follows
    const double speed = clip.effectiveSpeed();
    return speed >= kMinCurveSpeed - 1e-9 && speed <= kMaxCurveSpeed + 1e-9;
}

bool isFreezeFrameClip(const Clip &clip)
{
    return !clip.hasSpeedCurve() && clip.speed > 0.0 && clip.speed < kFreezeSpeedCeiling;
}

void makeFreezeFrame(Clip &clip, TimeUs sourceUs, TimeUs durationUs)
{
    durationUs = qMax<TimeUs>(1, durationUs);
    clip.speedCurve.clear();
    clip.reverse = false;
    clip.srcIn = qMax<TimeUs>(0, sourceUs);
    clip.srcOut = clip.srcIn + 1;
    clip.timelineDuration = durationUs;
    clip.speed = static_cast<double>(clip.srcOut - clip.srcIn) / static_cast<double>(durationUs);
    clip.fadeInUs = 0;
    clip.fadeOutUs = 0;
    clip.audioFadeInUs = 0;
    clip.audioFadeOutUs = 0;
    clip.animIn = ClipAnimation{};
    clip.animOut = ClipAnimation{};
    // Keys are clip-relative and a clip holds its first key's value before that key, so moving
    // every key past the held length keeps each property where it was at the frozen moment for the
    // whole freeze (a split tail already carries a key pinned at the cut, at 0).
    shiftClipKeyframes(clip, durationUs);
}

void setFreezeFrameLength(Clip &clip, TimeUs durationUs)
{
    durationUs = qMax<TimeUs>(1, durationUs);
    const TimeUs old = clip.timelineDuration;
    clip.timelineDuration = durationUs;
    clip.speed = static_cast<double>(qMax<TimeUs>(1, clip.srcOut - clip.srcIn)) / static_cast<double>(durationUs);
    // The keys sit past the old length; a longer freeze moves them past the new one too.
    if (durationUs > old)
        shiftClipKeyframes(clip, durationUs - old);
    clip.fadeInUs = qMin(clip.fadeInUs, durationUs);
    clip.fadeOutUs = qMin(clip.fadeOutUs, durationUs - clip.fadeInUs);
}

const QList<SpeedRampPreset> &speedRampPresets()
{
    // Built on first use so the labels are translated in the running locale.
    static const QList<SpeedRampPreset> presets = [] {
        QList<SpeedRampPreset> list;
        // Normal → fast → slow → normal.
        list.append({QStringLiteral("montage"), trPreset("Montage"),
                     smoothRamp({{0.0, 1.0}, {0.2, 1.0}, {0.4, 5.0}, {0.6, 0.4}, {0.8, 1.0}, {1.0, 1.0}})});
        // Fast in, a slow-motion moment in the middle, fast out.
        list.append({QStringLiteral("hero"), trPreset("Hero"),
                     smoothRamp({{0.0, 1.0}, {0.25, 5.0}, {0.5, 0.3}, {0.75, 5.0}, {1.0, 1.0}})});
        // Brisk, then almost still in the middle, then brisk again.
        list.append({QStringLiteral("bulletTime"), trPreset("Bullet Time"),
                     smoothRamp({{0.0, 2.0}, {0.35, 2.0}, {0.5, 0.2}, {0.65, 2.0}, {1.0, 2.0}})});
        // Slow, a sudden jump forward in the middle, slow again.
        list.append({QStringLiteral("jumper"), trPreset("Jumper"),
                     smoothRamp({{0.0, 0.5}, {0.38, 0.5}, {0.5, 6.0}, {0.62, 0.5}, {1.0, 0.5}})});
        // Rushes in, then settles to normal speed.
        list.append({QStringLiteral("flashIn"), trPreset("Flash in"),
                     smoothRamp({{0.0, 5.0}, {0.3, 5.0}, {0.6, 1.0}, {1.0, 1.0}})});
        // Normal speed, then rushes out.
        list.append({QStringLiteral("flashOut"), trPreset("Flash out"),
                     smoothRamp({{0.0, 1.0}, {0.4, 1.0}, {0.7, 5.0}, {1.0, 5.0}})});
        return list;
    }();
    return presets;
}

SpeedCurve speedRampPresetCurve(const QString &id)
{
    SpeedCurve curve;
    for (const SpeedRampPreset &preset : speedRampPresets()) {
        if (preset.id == id) {
            curve.setPoints(preset.points);
            break;
        }
    }
    return curve;
}

QString matchSpeedRampPreset(const SpeedCurve &curve)
{
    if (curve.isEmpty())
        return QStringLiteral("none");
    const QList<SpeedPoint> &points = curve.points();
    for (const SpeedRampPreset &preset : speedRampPresets()) {
        // Compared after the same clamping setPoints applies, so a preset reads back as itself.
        const QList<SpeedPoint> &want = speedRampPresetCurve(preset.id).points();
        if (want.size() != points.size())
            continue;
        bool same = true;
        for (int i = 0; same && i < want.size(); ++i) {
            same = std::abs(want.at(i).pos - points.at(i).pos) < 1e-3
                   && std::abs(want.at(i).speed - points.at(i).speed) < 1e-3;
        }
        if (same)
            return preset.id;
    }
    return QStringLiteral("custom");
}

QString speedKind(const Clip &clip)
{
    if (isFreezeFrameClip(clip))
        return QStringLiteral("freeze");
    if (clip.hasSpeedCurve())
        return QStringLiteral("ramp");
    const double speed = clip.effectiveSpeed();
    if (std::abs(speed - 1.0) < 0.005)
        return QStringLiteral("normal");
    return speed > 1.0 ? QStringLiteral("fast") : QStringLiteral("slow");
}

} // namespace drift
