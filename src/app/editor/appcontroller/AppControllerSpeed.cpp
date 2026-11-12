// Dafin: Filmora-style speed controls.
//
//   - Uniform speed from 0.01× to 100× (Speed tab), set as a speed or as the length the clip
//     should have. Sound plays between 0.1× and 8× (what the stretcher follows) and is left out
//     beyond, so it never drifts off its picture (drift::clipAudioFollowsSpeed).
//   - Speed ramping presets (Montage, Hero, Bullet Time, Jumper, Flash in, Flash out) and an inline
//     ramp graph, applied to the clip itself rather than to a copy on a new track, as Filmora does.
//   - Freeze frame: the frame at the playhead held inside the clip, the rest of it pushed later.
//   - Right-click › Speed › Show Speed Control: a strip on the clip ("Normal 1.00×") whose ends
//     retime the clip instead of trimming it ("Fast 1.63×"), as in Filmora.
//
// A retime that makes a clip longer moves what follows it on its tracks along when it would
// otherwise run into it, and with ripple on what follows always closes up or moves along.

#include "app/editor/appcontroller/AppControllerInternal.h"
#include "core/animation/SpeedRamp.h"
#include "core/edit/TimelineOps.h"

#include <QJsonArray>
#include <QJsonValue>

#include <cmath>
#include <limits>

using namespace drift::appdetail;
using namespace drift::appcontroller_detail;

namespace {

bool retimableClip(const drift::Clip &clip)
{
    return clip.type == drift::ClipType::Video || clip.type == drift::ClipType::Audio
           || clip.type == drift::ClipType::Composite;
}

// Ramps are offered where the engine plays them: media clips (the curve preview and the reverse
// render cannot read a composite's nested timeline either).
bool rampableClip(const drift::Clip &clip)
{
    return clip.type == drift::ClipType::Video || clip.type == drift::ClipType::Audio;
}

drift::SpeedCurve curveFromVariant(const QVariantList &points)
{
    return drift::SpeedCurve::fromJson(QJsonArray::fromVariantList(points));
}

QVariantList curveToVariant(const drift::SpeedCurve &curve)
{
    return curve.toJson().toVariantList();
}

// The tracks and ids a clip moves with: itself and its linked partners.
void clipGroup(const drift::Project &project, const drift::Clip &clip, QSet<int> *tracks, QSet<QString> *ids,
               int trackIndex)
{
    tracks->insert(trackIndex);
    ids->insert(clip.id);
    for (const drift::ClipRef &ref : drift::linkedPartners(project, clip)) {
        tracks->insert(ref.trackIndex);
        ids->insert(project.tracks().at(ref.trackIndex).clips.at(ref.clipIndex).id);
    }
}

} // namespace

double AppController::minUniformSpeed() const
{
    return drift::kMinUniformSpeed;
}

double AppController::maxUniformSpeed() const
{
    return drift::kMaxUniformSpeed;
}

void AppController::settleRetimedClip(const QString &clipId, drift::TimeUs oldEnd)
{
    int trackIndex = -1;
    int clipIndex = -1;
    if (!findClipById(m_project, clipId, &trackIndex, &clipIndex))
        return;
    const drift::Clip clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    const drift::TimeUs newEnd = clip.timelineEnd();
    const drift::TimeUs delta = newEnd - oldEnd;
    if (delta == 0)
        return;

    QSet<int> tracks;
    QSet<QString> ids;
    clipGroup(m_project, clip, &tracks, &ids, trackIndex);

    // Without ripple a shorter clip leaves its gap (as Filmora does), and a longer one only moves
    // what it would otherwise run into.
    bool collides = false;
    if (delta > 0) {
        for (const int t : std::as_const(tracks)) {
            for (const drift::Clip &other : m_project.tracks().at(t).clips) {
                if (!ids.contains(other.id) && other.timelineStart >= oldEnd && other.timelineStart < newEnd)
                    collides = true;
            }
        }
    }
    if (!m_rippleEnabled && !collides)
        return;

    QList<drift::Clip> moved;
    QSet<QString> movedIds;
    for (const int t : std::as_const(tracks)) {
        for (drift::Clip &other : m_project.tracks()[t].clips) {
            if (ids.contains(other.id) || other.timelineStart < oldEnd)
                continue;
            other.timelineStart = qMax<drift::TimeUs>(0, other.timelineStart + delta);
            moved.append(other);
            movedIds.insert(other.id);
        }
    }
    for (const drift::Clip &other : std::as_const(moved))
        syncLinkedPartnersFrom(m_project, other, movedIds + ids);
}

bool AppController::writeClipSpeedCurve(int trackIndex, int clipIndex, const drift::SpeedCurve &curve)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return false;
    drift::Clip &clip = m_project.tracks()[trackIndex].clips[clipIndex];
    if (!rampableClip(clip) || drift::isFreezeFrameClip(clip))
        return false;
    const drift::Clip old = clip;
    clip.speedCurve = curve;
    if (curve.isEmpty()) {
        // Back to the scalar speed over the same source range (as clearClipSpeedCurve does).
        clip.timelineDuration = qMax<drift::TimeUs>(
            1, llround(static_cast<double>(clip.srcOut - clip.srcIn) / clip.effectiveSpeed()));
    } else {
        clip.syncDurationFromSpeedCurve();
    }
    remapKeyframesForRetime(clip, old);
    syncLinkedPartnersFrom(m_project, clip);
    return true;
}

void AppController::setClipDurationSeconds(int trackIndex, int clipIndex, double seconds)
{
    if (!isValidClipIndex(trackIndex, clipIndex) || !(seconds > 0.0))
        return;
    {
        const drift::Clip &probe = m_project.tracks().at(trackIndex).clips.at(clipIndex);
        if (!retimableClip(probe))
            return;
        if (probe.hasSpeedCurve()) {
            setLastMessage(tr("A speed ramp sets this clip's length — remove the ramp to set it here"),
                           QStringLiteral("warning"));
            return;
        }
    }

    const drift::Project before = m_project;
    drift::Clip &clip = m_project.tracks()[trackIndex].clips[clipIndex];
    const drift::Clip old = clip;
    const drift::TimeUs oldEnd = clip.timelineEnd();
    const drift::TimeUs span = qMax<drift::TimeUs>(1, clip.srcOut - clip.srcIn);
    const drift::TimeUs wanted = qMax(drift::minClipDurationUs(m_project.fps()), drift::secondsToUs(seconds));
    if (drift::isFreezeFrameClip(clip)) {
        drift::setFreezeFrameLength(clip, wanted);
    } else {
        clip.speed = qBound(drift::kMinUniformSpeed,
                            static_cast<double>(span) / static_cast<double>(wanted), drift::kMaxUniformSpeed);
        clip.syncSrcOutFromSpeed(sourceDurationForClip(clip));
        remapKeyframesForRetime(clip, old);
    }
    const QString id = clip.id;
    syncLinkedPartnersFrom(m_project, clip);
    drift::retimeLinkedSubtitles(before, m_project);
    settleRetimedClip(id, oldEnd);
    pushProjectEdit(before, tr("Duration changed"));
    finishEdit(tr("Clip length updated"));
}

QVariantList AppController::speedRampPresets() const
{
    QVariantList out;
    for (const drift::SpeedRampPreset &preset : drift::speedRampPresets()) {
        out.append(QVariantMap{{QStringLiteral("id"), preset.id},
                               {QStringLiteral("label"), preset.label},
                               {QStringLiteral("points"), curveToVariant(drift::speedRampPresetCurve(preset.id))}});
    }
    return out;
}

QString AppController::clipSpeedRampPreset(int trackIndex, int clipIndex) const
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return QStringLiteral("none");
    return drift::matchSpeedRampPreset(m_project.tracks().at(trackIndex).clips.at(clipIndex).speedCurve);
}

void AppController::applySpeedRampPreset(int trackIndex, int clipIndex, const QString &presetId)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;
    const drift::Clip &probe = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    if (!rampableClip(probe) || drift::isFreezeFrameClip(probe))
        return;
    const bool clearing = presetId == QLatin1String("none");
    if (clearing && !probe.hasSpeedCurve())
        return;
    const drift::SpeedCurve curve = clearing ? drift::SpeedCurve{} : drift::speedRampPresetCurve(presetId);
    if (!clearing && curve.isEmpty())
        return;

    const drift::Project before = m_project;
    const QString id = probe.id;
    const drift::TimeUs oldEnd = probe.timelineEnd();
    if (!writeClipSpeedCurve(trackIndex, clipIndex, curve))
        return;
    drift::retimeLinkedSubtitles(before, m_project);
    settleRetimedClip(id, oldEnd);
    QString label;
    for (const drift::SpeedRampPreset &preset : drift::speedRampPresets()) {
        if (preset.id == presetId)
            label = preset.label;
    }
    const QString text = clearing ? tr("Speed ramp removed") : tr("Speed ramp: %1").arg(label);
    pushProjectEdit(before, text);
    finishEdit(text);
}

QVariantList AppController::clipSpeedCurvePoints(int trackIndex, int clipIndex) const
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return {};
    const drift::Clip &clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    return clip.hasSpeedCurve() ? curveToVariant(clip.speedCurve) : QVariantList{};
}

void AppController::previewSetClipSpeedCurvePoints(int trackIndex, int clipIndex, const QVariantList &points)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;
    if (!m_previewDragActive)
        beginPreviewDrag(tr("Speed ramp changed"));
    const drift::SpeedCurve curve = curveFromVariant(points);
    if (curve.isEmpty())
        return;
    if (writeClipSpeedCurve(trackIndex, clipIndex, curve))
        emitPreviewFrame();
}

void AppController::setClipSpeedCurvePoints(int trackIndex, int clipIndex, const QVariantList &points)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;
    const drift::SpeedCurve curve = curveFromVariant(points);
    const drift::Clip &probe = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    if (!rampableClip(probe))
        return;
    const drift::Project before = m_project;
    const QString id = probe.id;
    const drift::TimeUs oldEnd = probe.timelineEnd();
    if (!writeClipSpeedCurve(trackIndex, clipIndex, curve))
        return;
    drift::retimeLinkedSubtitles(before, m_project);
    settleRetimedClip(id, oldEnd);
    pushProjectEdit(before, tr("Speed ramp changed"));
    finishEdit(tr("Speed ramp changed"));
}

void AppController::insertFreezeFrame(int trackIndex, int clipIndex, double seconds)
{
    if (trackIndex < 0 && clipIndex < 0) {
        trackIndex = m_selectedTrack;
        clipIndex = m_selectedClip;
    }
    if (!isValidClipIndex(trackIndex, clipIndex)) {
        setLastMessage(tr("Select a video clip to freeze its frame"), QStringLiteral("warning"));
        return;
    }
    drift::TimeUs offset = 0;
    {
        const drift::Clip &probe = m_project.tracks().at(trackIndex).clips.at(clipIndex);
        if (probe.type != drift::ClipType::Video) {
            setLastMessage(tr("Freeze frame works on video clips"), QStringLiteral("warning"));
            return;
        }
        if (drift::isFreezeFrameClip(probe)) {
            setLastMessage(tr("This part is already a freeze frame — change its Duration in Speed"),
                           QStringLiteral("warning"));
            return;
        }
        if (m_playheadUs < probe.timelineStart || m_playheadUs >= probe.timelineEnd()) {
            setLastMessage(tr("Move the playhead onto the clip, to the frame to freeze"),
                           QStringLiteral("warning"));
            return;
        }
        offset = m_playheadUs - probe.timelineStart;
    }
    setPlaying(false);

    const double fps = m_project.fps();
    const drift::TimeUs holdUs = qMax(drift::minClipDurationUs(fps), drift::secondsToUs(seconds));
    // A cut closer to an end than a frame would leave a sliver; the freeze goes at that end instead.
    const drift::TimeUs minEdge = drift::minClipDurationUs(fps);
    const bool atEnd = m_project.tracks().at(trackIndex).clips.at(clipIndex).timelineDuration - offset < minEdge;
    const auto build = [offset, holdUs, minEdge](const drift::Clip &clip) {
        QList<drift::Clip> segments;
        if (clip.timelineDuration - offset < minEdge) {
            // On the last frame: the clip plays out, then holds its last frame.
            drift::Clip hold = clip;
            const drift::TimeUs lastUs = qMax<drift::TimeUs>(0, clip.timelineDuration - 1);
            const drift::TimeUs sourceUs = clip.timelineToSourceUs(clip.timelineStart + lastUs);
            drift::rebaseKeyframesForSplitTail(hold, lastUs);
            hold.timelineStart = clip.timelineEnd();
            drift::makeFreezeFrame(hold, sourceUs, holdUs);
            segments.append(clip);
            segments.append(hold);
            return segments;
        }
        drift::Clip head = clip;
        drift::Clip tail;
        const bool split = offset >= minEdge && drift::splitClipAtOffsetMin(head, tail, offset, minEdge);
        if (!split)
            tail = clip; // on the first frame: hold it, then play the clip
        drift::Clip hold = tail;
        drift::makeFreezeFrame(hold, tail.timelineToSourceUs(tail.timelineStart), holdUs);
        tail.timelineStart += holdUs;
        if (split)
            segments.append(head);
        segments.append(hold);
        segments.append(tail);
        return segments;
    };

    const drift::Project before = m_project;
    SegmentReplaceResult result;
    QString error;
    // Always rippled: the freeze is new time inside the clip, so the rest of it — and whatever
    // follows it on its tracks — moves later by exactly that much.
    if (!replaceClipGroupWithSegments(trackIndex, clipIndex, build, /*ripple=*/true, &result, &error)) {
        m_project = before;
        setLastMessage(tr("Couldn’t add a freeze frame here"), QStringLiteral("error"));
        return;
    }
    drift::retimeLinkedSubtitles(before, m_project);
    pushProjectEdit(before, tr("Freeze frame added"));
    finishEdit(tr("Freeze frame added"));

    // Select the held frame, so Speed › Duration is right there to change its length.
    const int holdIndex = (result.ids.size() >= 3 || atEnd) ? 1 : 0;
    selectClipById(result.ids.value(holdIndex));
}

bool AppController::speedControlVisible(int trackIndex, int clipIndex) const
{
    return isValidClipIndex(trackIndex, clipIndex)
           && m_speedControlIds.contains(m_project.tracks().at(trackIndex).clips.at(clipIndex).id);
}

void AppController::setSpeedControlVisible(int trackIndex, int clipIndex, bool visible)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;
    const drift::Clip &clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    if (!retimableClip(clip))
        return;
    const bool had = m_speedControlIds.contains(clip.id);
    if (had == visible)
        return;
    if (visible)
        m_speedControlIds.insert(clip.id);
    else
        m_speedControlIds.remove(clip.id);
    notifyTracksChanged();
}

QVariantMap AppController::previewClipRetime(int trackIndex, int clipIndex, int edge, double seconds) const
{
    QVariantMap out{{QStringLiteral("ok"), false}};
    if (!isValidClipIndex(trackIndex, clipIndex) || edge == 0)
        return out;
    const drift::Clip &clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    if (!retimableClip(clip) || clip.hasSpeedCurve())
        return out;

    const bool freeze = drift::isFreezeFrameClip(clip);
    const drift::TimeUs span = qMax<drift::TimeUs>(1, clip.srcOut - clip.srcIn);
    const drift::TimeUs start = clip.timelineStart;
    const drift::TimeUs end = clip.timelineEnd();
    const drift::TimeUs minDur = drift::minClipDurationUs(m_project.fps());
    const drift::TimeUs target = drift::secondsToUs(seconds);

    // How far the moving end may go before it would run into a neighbour on the clip's tracks.
    // With ripple on the end edge pushes what follows instead.
    QSet<int> tracks;
    QSet<QString> ids;
    clipGroup(m_project, clip, &tracks, &ids, trackIndex);
    drift::TimeUs limitEnd = std::numeric_limits<drift::TimeUs>::max();
    drift::TimeUs limitStart = 0;
    for (const int t : std::as_const(tracks)) {
        for (const drift::Clip &other : m_project.tracks().at(t).clips) {
            if (ids.contains(other.id))
                continue;
            if (other.timelineStart >= end)
                limitEnd = qMin(limitEnd, other.timelineStart);
            if (other.timelineEnd() <= start)
                limitStart = qMax(limitStart, other.timelineEnd());
        }
    }
    if (m_rippleEnabled)
        limitEnd = std::numeric_limits<drift::TimeUs>::max();

    drift::TimeUs duration = edge > 0 ? qMax(minDur, target - start) : qMax(minDur, end - target);
    if (edge > 0)
        duration = qMin(duration, limitEnd - start);
    else
        duration = qMin(duration, end - limitStart);
    duration = qMax(minDur, duration);

    double speed = static_cast<double>(span) / static_cast<double>(duration);
    if (!freeze) {
        speed = qBound(drift::kMinUniformSpeed, speed, drift::kMaxUniformSpeed);
        duration = qMax<drift::TimeUs>(1, llround(static_cast<double>(span) / speed));
    }
    const drift::TimeUs newStart = edge > 0 ? start : end - duration;

    QString kind = freeze ? QStringLiteral("freeze")
                   : std::abs(speed - 1.0) < 0.005 ? QStringLiteral("normal")
                   : speed > 1.0 ? QStringLiteral("fast") : QStringLiteral("slow");
    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("start"), drift::usToSeconds(newStart));
    out.insert(QStringLiteral("duration"), drift::usToSeconds(duration));
    out.insert(QStringLiteral("speed"), speed);
    out.insert(QStringLiteral("kind"), kind);
    return out;
}

bool AppController::retimeClipEdge(int trackIndex, int clipIndex, int edge, double seconds)
{
    const QVariantMap plan = previewClipRetime(trackIndex, clipIndex, edge, seconds);
    if (!plan.value(QStringLiteral("ok")).toBool())
        return false;

    const drift::Project before = m_project;
    drift::Clip &clip = m_project.tracks()[trackIndex].clips[clipIndex];
    const drift::Clip old = clip;
    const drift::TimeUs oldEnd = clip.timelineEnd();
    const drift::TimeUs duration = drift::secondsToUs(plan.value(QStringLiteral("duration")).toDouble());
    if (drift::isFreezeFrameClip(clip)) {
        drift::setFreezeFrameLength(clip, duration);
    } else {
        clip.speed = plan.value(QStringLiteral("speed")).toDouble();
        clip.syncSrcOutFromSpeed(sourceDurationForClip(clip));
        remapKeyframesForRetime(clip, old);
    }
    if (edge < 0)
        clip.timelineStart = qMax<drift::TimeUs>(0, oldEnd - clip.timelineDuration);
    if (clip.timelineStart == old.timelineStart && clip.timelineDuration == old.timelineDuration
        && qFuzzyCompare(clip.speed, old.speed)) {
        m_project = before;
        return false;
    }
    const QString id = clip.id;
    syncLinkedPartnersFrom(m_project, clip);
    drift::retimeLinkedSubtitles(before, m_project);
    if (edge > 0)
        settleRetimedClip(id, oldEnd);
    pushProjectEdit(before, tr("Speed changed"));
    finishEdit(tr("Clip speed updated"));
    return true;
}

void AppController::fillSpeedStrip(TimelineClipsModel::Row &row, const drift::Clip &clip) const
{
    row.speedKind.clear();
    row.speedLabel.clear();
    row.speedControl = false;
    if (!retimableClip(clip) || !clip.mogrt.isEmpty())
        return;
    const QString kind = drift::speedKind(clip);
    row.speedControl = m_speedControlIds.contains(clip.id);
    if (kind == QLatin1String("normal") && !clip.reverse && !row.speedControl)
        return;

    const QString rate = QString::number(clip.effectiveSpeed(), 'f', 2) + QStringLiteral("×");
    QString label;
    if (kind == QLatin1String("freeze")) {
        label = tr("Freeze %1 s").arg(QString::number(drift::usToSeconds(clip.timelineDuration), 'f', 1));
    } else if (kind == QLatin1String("ramp")) {
        const QString preset = drift::matchSpeedRampPreset(clip.speedCurve);
        QString name;
        for (const drift::SpeedRampPreset &entry : drift::speedRampPresets()) {
            if (entry.id == preset)
                name = entry.label;
        }
        label = name.isEmpty() ? tr("Speed ramp") : tr("Ramp: %1").arg(name);
    } else if (kind == QLatin1String("fast")) {
        label = tr("Fast %1").arg(rate);
    } else if (kind == QLatin1String("slow")) {
        label = tr("Slow %1").arg(rate);
    } else {
        label = tr("Normal %1").arg(rate);
    }
    if (clip.reverse)
        label += tr(" · Reverse");
    row.speedKind = kind;
    row.speedLabel = label;
}
