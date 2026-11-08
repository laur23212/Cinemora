// Dafin additions to the editor controller: Filmora-style keyframing from the timeline and
// titles made from imported Motion Graphics Templates.
//
// Keyframes, the Filmora way:
//   - The timeline toolbar's keyframe button (Alt+K, as in Filmora) keys position, scale, rotation and opacity of the
//     clip at the playhead in one go, or removes the keyframe the playhead sits on.
//   - Every clip shows its keyframes as diamonds on the timeline (TimelineKeyframeMarkers.qml);
//     drag one to retime it, right-click it to delete it or clear them all.
//   - Once a property has keyframes, changing it at another time adds a keyframe there
//     (autoKeyFor), so animating is: key, move the playhead, change the clip.
//
// Drift keeps a property's static value as a lone key at the clip's start. The first keyframe
// the button makes takes that key's place, so the property holds its value before the first
// keyframe and the clip shows only the diamonds the user made — as in Filmora.

#include "app/editor/appcontroller/AppControllerInternal.h"
#include "app/editor/sessions/KeyframeController.h"
#include "core/edit/TimelineOps.h"
#include "core/text/TextStyle.h"

#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace drift::appdetail;
using namespace drift::appcontroller_detail;

namespace {

// Filmora's Transform and Compositing values: what one press of the keyframe button keys.
const QStringList &keyframeButtonProps()
{
    static const QStringList props{QStringLiteral("x"),      QStringLiteral("y"),
                                   QStringLiteral("width"),  QStringLiteral("height"),
                                   QStringLiteral("rotation"), QStringLiteral("opacity")};
    return props;
}

// Keys of different properties closer than this are one diamond on the timeline.
constexpr drift::TimeUs kDiamondMergeUs = drift::kUsPerSecond / 100;

bool coversTime(const drift::Clip &clip, drift::TimeUs t)
{
    return t >= clip.timelineStart && t <= clip.timelineEnd();
}

} // namespace

bool AppController::autoKeyFor(const drift::Clip &clip, const QString &prop,
                               const drift::KeyframeTrack<double> *track) const
{
    if (m_autoKeyEnabled)
        return true;
    if (m_keyframedClipIds.contains(clip.id) && keyframeButtonProps().contains(prop))
        return true;
    if (!track)
        track = propKeyframeTrack(clip, prop);
    if (!track || !track->enabled())
        return false;
    // Animated: more than one key, or one that is not the static value at the clip's start.
    const QMap<drift::TimeUs, drift::Keyframe<double>> &keys = track->keyframes();
    return keys.size() > 1 || (keys.size() == 1 && keys.firstKey() != 0);
}

// The properties a timeline diamond stands for: everything animated on the clip except volume,
// plus the button's properties on a clip keyframed at its start this session.
static QStringList diamondProps(const QStringList &animated, bool keyframedHere)
{
    QStringList props;
    for (const QString &prop : animated) {
        if (prop != QLatin1String("volume"))
            props.append(prop);
    }
    if (keyframedHere) {
        for (const QString &prop : keyframeButtonProps()) {
            if (!props.contains(prop))
                props.append(prop);
        }
    }
    return props;
}

QVariantList AppController::clipKeyframeTimes(int trackIndex, int clipIndex) const
{
    QVariantList out;
    if (!isValidClipIndex(trackIndex, clipIndex))
        return out;
    const drift::Clip &clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    QList<drift::TimeUs> times;
    const QStringList props = diamondProps(clipAnimatedProperties(trackIndex, clipIndex),
                                           m_keyframedClipIds.contains(clip.id));
    for (const QString &prop : props) {
        int hostTrack = trackIndex;
        int hostClip = clipIndex;
        redirectToKeyframeHost(&hostTrack, &hostClip, prop);
        if (!isValidClipIndex(hostTrack, hostClip))
            continue;
        const drift::Clip &host = m_project.tracks().at(hostTrack).clips.at(hostClip);
        const drift::KeyframeTrack<double> *kt = propKeyframeTrack(host, prop);
        if (!kt)
            continue;
        const QMap<drift::TimeUs, drift::Keyframe<double>> &keys = kt->keyframes();
        for (auto it = keys.constBegin(); it != keys.constEnd(); ++it) {
            const drift::TimeUs rel = host.timelineStart + it.key() - clip.timelineStart;
            if (rel < -kKeyframeToleranceUs || rel > clip.timelineDuration + kKeyframeToleranceUs)
                continue; // outside the clip: never drawn, never reached
            times.append(qBound<drift::TimeUs>(0, rel, clip.timelineDuration));
        }
    }
    std::sort(times.begin(), times.end());
    drift::TimeUs last = std::numeric_limits<drift::TimeUs>::min();
    for (const drift::TimeUs t : std::as_const(times)) {
        if (!out.isEmpty() && t - last <= kDiamondMergeUs)
            continue;
        out.append(drift::usToSeconds(t));
        last = t;
    }
    return out;
}

bool AppController::clipHasKeyframeAt(int trackIndex, int clipIndex, double atSeconds) const
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return false;
    const drift::Clip &clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    const double rel = atSeconds - drift::usToSeconds(clip.timelineStart);
    const double tolerance = drift::usToSeconds(kKeyframeToleranceUs);
    for (const QVariant &t : clipKeyframeTimes(trackIndex, clipIndex)) {
        if (std::abs(t.toDouble() - rel) <= tolerance)
            return true;
    }
    return false;
}

QVariantList AppController::clipKeyframeRefsAt(int trackIndex, int clipIndex, double atSeconds) const
{
    QVariantList refs;
    if (!isValidClipIndex(trackIndex, clipIndex))
        return refs;
    const drift::Clip &clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    const QStringList props = diamondProps(clipAnimatedProperties(trackIndex, clipIndex),
                                           m_keyframedClipIds.contains(clip.id));
    for (const QString &prop : props) {
        int hostTrack = trackIndex;
        int hostClip = clipIndex;
        redirectToKeyframeHost(&hostTrack, &hostClip, prop);
        if (!isValidClipIndex(hostTrack, hostClip))
            continue;
        const drift::Clip &host = m_project.tracks().at(hostTrack).clips.at(hostClip);
        const drift::KeyframeTrack<double> *kt = propKeyframeTrack(host, prop);
        if (!kt)
            continue;
        const drift::TimeUs at = kt->nearestKeyframe(drift::secondsToUs(atSeconds) - host.timelineStart,
                                                     kKeyframeToleranceUs);
        if (at < 0)
            continue;
        refs.append(QVariantMap{{QStringLiteral("prop"), prop},
                                {QStringLiteral("seconds"), drift::usToSeconds(host.timelineStart + at)}});
    }
    return refs;
}

bool AppController::addTransformKeyframes(int trackIndex, int clipIndex, double atSeconds)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return false;
    drift::Clip &clip = m_project.tracks()[trackIndex].clips[clipIndex];
    if (!clipAcceptsPreviewTransform(clip))
        return false;
    const drift::Project before = m_project;
    const drift::TimeUs rel =
        qBound<drift::TimeUs>(0, drift::secondsToUs(atSeconds) - clip.timelineStart, clip.timelineDuration);

    bool changed = false;
    for (const QString &prop : keyframeButtonProps()) {
        // Read before the track is created, so an unkeyed property starts from what it shows now.
        const double base = propertyBaseValue(trackIndex, clipIndex, prop, 0.0);
        drift::KeyframeTrack<double> *kt = propKeyframeTrack(clip, prop, /*create=*/true);
        if (!kt)
            continue;
        if (!kt->isEmpty() && kt->nearestKeyframe(rel, kKeyframeToleranceUs) >= 0)
            continue; // already keyed here; the key keeps its value and tangents
        const double value = kt->isEmpty() ? base
                             : kt->enabled() ? kt->evaluateAt(rel)
                                             : kt->keyframes().first().value;
        const QMap<drift::TimeUs, drift::Keyframe<double>> &keys = kt->keyframes();
        if (keys.size() == 1 && keys.firstKey() == 0) {
            // The static value becomes the first keyframe instead of a second key beside it.
            drift::Keyframe<double> key = keys.first();
            key.value = value;
            kt->removeKeyframe(0);
            kt->setKeyframe(rel, key);
        } else {
            kt->setKeyframe(rel, value);
        }
        kt->setEnabled(true);
        changed = true;
    }

    m_keyframedClipIds.insert(clip.id);
    if (changed) {
        pushProjectEdit(before, tr("Add keyframe"));
        finishEdit(tr("Keyframe added"));
    } else {
        // At the clip's start the static values already are the first keyframe: nothing to store,
        // but the clip now counts as keyframed and its diamond appears.
        syncClipModels();
    }
    return true;
}

void AppController::removeClipKeyframesAt(int trackIndex, int clipIndex, double atSeconds)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;
    const QString clipId = m_project.tracks().at(trackIndex).clips.at(clipIndex).id;
    const bool keyframedHere = m_keyframedClipIds.contains(clipId);
    const QStringList props = diamondProps(clipAnimatedProperties(trackIndex, clipIndex), keyframedHere);
    const drift::Project before = m_project;

    bool changed = false;
    for (const QString &prop : props) {
        int hostTrack = trackIndex;
        int hostClip = clipIndex;
        redirectToKeyframeHost(&hostTrack, &hostClip, prop);
        if (!isValidClipIndex(hostTrack, hostClip))
            continue;
        drift::Clip &host = m_project.tracks()[hostTrack].clips[hostClip];
        drift::KeyframeTrack<double> *kt = propKeyframeTrack(host, prop, /*create=*/false);
        if (!kt || kt->isEmpty())
            continue;
        const drift::TimeUs at = kt->nearestKeyframe(drift::secondsToUs(atSeconds) - host.timelineStart,
                                                     kKeyframeToleranceUs);
        if (at < 0)
            continue;
        if (kt->keyframes().size() > 1) {
            kt->removeKeyframe(at);
            changed = true;
            continue;
        }
        if (at == 0)
            continue; // a lone key at the start is the static value itself
        // The last keyframe: the property keeps its value rather than falling back to a default
        // (a clip that loses its last position key would otherwise jump to the canvas origin).
        const double value = kt->keyframes().first().value;
        kt->removeKeyframe(at);
        kt->setEnabled(true);
        writeClipProp(host, prop, 0, value, /*autoKey=*/false, /*force=*/false);
        changed = true;
    }

    const auto stillKeyframed = [this, trackIndex, clipIndex]() {
        for (const QString &prop : clipAnimatedProperties(trackIndex, clipIndex)) {
            if (keyframeButtonProps().contains(prop))
                return true;
        }
        return false;
    };
    if (changed) {
        pushProjectEdit(before, tr("Delete keyframe"));
        if (!stillKeyframed())
            m_keyframedClipIds.remove(clipId);
        finishEdit(tr("Keyframe deleted"));
    } else if (keyframedHere && !stillKeyframed()) {
        // The diamond at the start of a clip keyframed there: dropping it stores nothing.
        m_keyframedClipIds.remove(clipId);
        syncClipModels();
    }
}

void AppController::clearClipKeyframes(int trackIndex, int clipIndex)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;
    const QString clipId = m_project.tracks().at(trackIndex).clips.at(clipIndex).id;
    QStringList props = clipAnimatedProperties(trackIndex, clipIndex);
    props.removeAll(QStringLiteral("volume"));
    m_keyframedClipIds.remove(clipId);
    if (props.isEmpty()) {
        syncClipModels();
        return;
    }
    // One undo step; each property keeps the value it shows at the playhead.
    m_keyframes->clearProperties(trackIndex, clipIndex, props);
}

void AppController::toggleKeyframeAtPlayhead()
{
    const drift::TimeUs now = m_playheadUs;
    int trackIndex = m_selectedTrack;
    int clipIndex = m_selectedClip;
    const auto usable = [this, now](int t, int c) {
        if (!isValidClipIndex(t, c))
            return false;
        const drift::Clip &clip = m_project.tracks().at(t).clips.at(c);
        return clipAcceptsPreviewTransform(clip) && coversTime(clip, now);
    };
    if (!usable(trackIndex, clipIndex)) {
        // Nothing usable selected: the topmost visible clip under the playhead, as Filmora keys
        // the clip you are looking at.
        trackIndex = -1;
        clipIndex = -1;
        const QList<drift::Track> &tracks = m_project.tracks();
        for (int t = 0; t < tracks.size() && trackIndex < 0; ++t) {
            if (tracks.at(t).hidden)
                continue;
            for (int c = 0; c < tracks.at(t).clips.size(); ++c) {
                const drift::Clip &clip = tracks.at(t).clips.at(c);
                if (clipAcceptsPreviewTransform(clip) && now >= clip.timelineStart && now < clip.timelineEnd()) {
                    trackIndex = t;
                    clipIndex = c;
                    break;
                }
            }
        }
        if (trackIndex < 0) {
            emit transformBlocked(tr("Move the playhead over a video, image, title or sticker clip to add a keyframe."));
            return;
        }
        selectClip(trackIndex, clipIndex);
    }
    const double at = drift::usToSeconds(now);
    if (clipHasKeyframeAt(trackIndex, clipIndex, at))
        removeClipKeyframesAt(trackIndex, clipIndex, at);
    else
        addTransformKeyframes(trackIndex, clipIndex, at);
}

void AppController::addTemplateTitle(const QString &text, const QString &presetId, double durationSeconds,
                                     const QString &name)
{
    QString content = text.trimmed();
    if (content.isEmpty())
        content = name.trimmed().isEmpty() ? tr("Your title here") : name.trimmed();
    const drift::TimeUs duration = durationSeconds >= 0.5
                                       ? qBound<drift::TimeUs>(drift::secondsToUs(0.5), drift::secondsToUs(durationSeconds),
                                                               drift::secondsToUs(300.0))
                                       : drift::kTextClipDurationUs;

    const drift::Project before = m_project;
    const int trackIndex = drift::ensureTrackForClipType(m_project, drift::ClipType::Text, true);
    if (trackIndex < 0)
        return;
    drift::Track &track = m_project.tracks()[trackIndex];
    const drift::TimeUs start =
        drift::resolveClipStart(m_project, track, -1, m_playheadUs, duration, m_snapEnabled, m_playheadUs);

    drift::Clip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.type = drift::ClipType::Text;
    clip.name = (name.trimmed().isEmpty() ? content.section(QLatin1Char('\n'), 0, 0) : name.trimmed()).left(32);
    clip.textContent = content;
    clip.timelineStart = start;
    clip.timelineDuration = duration;
    clip.srcIn = 0;
    clip.srcOut = duration;
    if (!presetId.isEmpty()) {
        if (const std::optional<drift::TextStyle> preset = drift::textStyleForPresetId(presetId)) {
            clip.textStyle = *preset;
            clip.textStyle.packId = presetId;
        }
    }
    applyDefaultVisualLayout(clip, m_project.width(), m_project.height());

    track.clips.append(clip);
    const int newClipIndex = int(track.clips.size()) - 1;
    pushProjectEdit(before, tr("Title added"));
    finishEdit(tr("Title added"));
    selectClip(trackIndex, newClipIndex);
    // resolveClipStart may push the title past whatever already sits at the playhead; follow it
    // so the preview shows the new title.
    if (m_playheadUs < start || m_playheadUs >= start + duration)
        setPlayheadSeconds(drift::usToSeconds(start));
}
