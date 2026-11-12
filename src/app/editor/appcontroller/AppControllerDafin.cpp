// Dafin additions to the editor controller: Filmora-style keyframing from the timeline and
// titles made from imported Motion Graphics Templates.
//
// Keyframes, the Filmora way:
//   - The timeline toolbar's keyframe button (Alt+K, as in Filmora) keys position, scale, rotation and opacity of the
//     clip at the playhead in one go, or removes the keyframe the playhead sits on. On a selected adjustment layer
//     it keys the layer's opacity, which is how strongly its effects apply.
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
#include "engine/decode/readers/ClipReaderPool.h"
#include "engine/packages/effects/EffectCatalog.h"

#include <QColor>
#include <QImageReader>
#include <QUuid>
#include <QtConcurrent>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

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

// An adjustment layer on its own track has no picture of its own to move: what the button keys on
// it is its opacity, how strongly its effects apply, so the effect can fade in and out.
bool isStandaloneEffectsLayer(const drift::Track &track, const drift::Clip &clip)
{
    return clip.type == drift::ClipType::Adjustment
           && clip.adjustmentKind == drift::AdjustmentKind::VideoEffects && track.isAdjustment()
           && !track.isAdjustmentLane() && !track.isTransformLayer() && !track.isCameraLayer();
}

const QStringList &keyframeButtonPropsFor(const drift::Track &track, const drift::Clip &clip)
{
    static const QStringList strengthOnly{QStringLiteral("opacity")};
    return isStandaloneEffectsLayer(track, clip) ? strengthOnly : keyframeButtonProps();
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

// Dafin motion tracking writes the position of an element that follows a tracked point (and its
// rotation and size for a rotate or scale solve). Those keys come from the track, so they are not
// drawn as the user's diamonds and a diamond never drags them.
static QStringList withoutTrackedProps(const drift::Project &project, const QString &clipId, QStringList props)
{
    for (const drift::Track &track : project.tracks()) {
        for (const drift::Clip &host : track.clips) {
            for (const drift::MotionTracker &tracker : host.motionTrackers) {
                if (!tracker.autoFollow)
                    continue;
                for (const drift::TrackerTarget &target : tracker.targets) {
                    if (target.mode != drift::TrackerTarget::Mode::FollowClip || target.clipId != clipId)
                        continue;
                    const bool planar = tracker.kind == drift::MotionTracker::Kind::Planar;
                    props.removeAll(QStringLiteral("x"));
                    props.removeAll(QStringLiteral("y"));
                    if (planar || tracker.solve != drift::MotionTracker::Solve::Translate)
                        props.removeAll(QStringLiteral("rotation"));
                    if (planar || tracker.solve == drift::MotionTracker::Solve::TranslateRotateScale
                        || tracker.solve == drift::MotionTracker::Solve::Perspective) {
                        props.removeAll(QStringLiteral("width"));
                        props.removeAll(QStringLiteral("height"));
                    }
                    return props;
                }
            }
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
    const QStringList props = withoutTrackedProps(m_project, clip.id,
                                                  diamondProps(clipAnimatedProperties(trackIndex, clipIndex),
                                                               m_keyframedClipIds.contains(clip.id)));
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
    const QStringList props = withoutTrackedProps(m_project, clip.id,
                                                  diamondProps(clipAnimatedProperties(trackIndex, clipIndex),
                                                               m_keyframedClipIds.contains(clip.id)));
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
    const drift::Track &owner = m_project.tracks().at(trackIndex);
    if (!clipAcceptsPreviewTransform(clip) && !isStandaloneEffectsLayer(owner, clip))
        return false;
    const drift::Project before = m_project;
    const drift::TimeUs rel =
        qBound<drift::TimeUs>(0, drift::secondsToUs(atSeconds) - clip.timelineStart, clip.timelineDuration);

    bool changed = false;
    for (const QString &prop : keyframeButtonPropsFor(owner, clip)) {
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
        // A selected adjustment layer keys its own strength rather than the clip under it.
        return (clipAcceptsPreviewTransform(clip) || isStandaloneEffectsLayer(m_project.tracks().at(t), clip))
               && coversTime(clip, now);
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
    int trackIndex = drift::ensureTrackForClipType(m_project, drift::ClipType::Text, true);
    if (trackIndex < 0)
        return;
    // A second title at the same moment goes on a title track of its own, above the first.
    {
        const StackPlan plan = stackPlan(trackIndex, drift::ClipType::Text, m_playheadUs, duration);
        if (plan.insertIndex >= 0)
            trackIndex = drift::insertTrackAboveForClipType(m_project, plan.insertIndex, drift::ClipType::Text);
        else if (plan.track >= 0)
            trackIndex = plan.track;
    }
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
        // Dafin: through the template path, so a title template's clip effects come along.
        if (const std::optional<drift::TextPreset> preset = drift::textPresetForId(presetId))
            applyTitleTemplate(clip, *preset);
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

// ---- Chroma key (the timeline toolbar's button) ----------------------------------------------
//
// Drift already keys green and blue screens (effects/chroma_key, a hue key with spill removal and
// an eyedropper in the Effects inspector). The button puts it on the selected clip in one click,
// with the backdrop colour read from the frame at the playhead, and switches it off and on again.

namespace {

const QString &chromaKeyEffectId()
{
    static const QString id = QStringLiteral("key.chroma");
    return id;
}

// A decoder of its own, so reading the frame does not disturb the preview's.
constexpr quint64 kChromaStreamId = 0xDAF1'0C4A'0000'0001ull;

QImage chromaAnalysisFrame(const QString &path, drift::ClipType type, drift::TimeUs sourceUs, int rotation)
{
    constexpr int kSide = 320;
    if (type == drift::ClipType::Image) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QSize size = reader.size();
        if (size.isValid() && (size.width() > kSide || size.height() > kSide))
            reader.setScaledSize(size.scaled(kSide, kSide, Qt::KeepAspectRatio));
        return reader.read();
    }
    // A frame timestamp is rarely a whole microsecond: nudge past it so the read lands on it.
    return ClipReaderPool::instance().readVideoFrame(path, kChromaStreamId, sourceUs + 1000, kSide, kSide, rotation);
}

// The backdrop's hue in degrees: the dominant strongly saturated hue of the frame, its outer fifth
// counting three times (the subject usually stands in the middle). NaN when no hue dominates, e.g.
// a frame with no screen in it.
double dominantBackdropHue(const QImage &source)
{
    const double none = std::numeric_limits<double>::quiet_NaN();
    if (source.isNull())
        return none;
    const QImage image = source.convertToFormat(QImage::Format_RGB32);
    constexpr int kBins = 72; // 5 degrees each
    std::array<double, kBins> weight{};
    double total = 0.0;
    const int w = image.width();
    const int h = image.height();
    const int step = std::max(1, std::max(w, h) / 160);
    for (int y = 0; y < h; y += step) {
        const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < w; x += step) {
            const QColor colour = QColor::fromRgb(row[x]);
            const float hue = colour.hsvHueF();
            const float saturation = colour.hsvSaturationF();
            if (hue < 0.0f || saturation < 0.25f || colour.valueF() < 0.15f)
                continue;
            const bool edge = x < w / 5 || x >= w - w / 5 || y < h / 5 || y >= h - h / 5;
            const double vote = saturation * (edge ? 3.0 : 1.0);
            weight[std::min(kBins - 1, int(hue * kBins))] += vote;
            total += vote;
        }
    }
    if (total <= 0.0)
        return none;
    int best = 0;
    double bestMass = -1.0;
    for (int i = 0; i < kBins; ++i) {
        const double mass = weight[(i + kBins - 1) % kBins] + weight[i] + weight[(i + 1) % kBins];
        if (mass > bestMass) {
            bestMass = mass;
            best = i;
        }
    }
    if (bestMass < 0.2 * total)
        return none;
    double sx = 0.0;
    double sy = 0.0;
    for (int d = -2; d <= 2; ++d) {
        const int i = (best + d + kBins) % kBins;
        const double angle = (i + 0.5) * (2.0 * std::numbers::pi / kBins);
        sx += weight[i] * std::cos(angle);
        sy += weight[i] * std::sin(angle);
    }
    double degrees = std::atan2(sy, sx) * 180.0 / std::numbers::pi;
    if (degrees < 0.0)
        degrees += 360.0;
    return degrees;
}

} // namespace

QVariantMap AppController::chromaKeyInfo(int trackIndex, int clipIndex) const
{
    QVariantMap out{{QStringLiteral("eligible"), false},
                    {QStringLiteral("present"), false},
                    {QStringLiteral("enabled"), false},
                    {QStringLiteral("effectIndex"), -1}};
    if (!isValidClipIndex(trackIndex, clipIndex))
        return out;
    const drift::Clip &clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    const drift::Clip *stack = nullptr;
    if (clip.type == drift::ClipType::Adjustment && clip.adjustmentKind == drift::AdjustmentKind::VideoEffects)
        stack = &clip;
    else if (clip.type == drift::ClipType::Video || clip.type == drift::ClipType::Image)
        stack = effectHostClip(trackIndex, clipIndex, drift::AdjustmentKind::VideoEffects);
    else
        return out;
    out.insert(QStringLiteral("eligible"), true);
    if (!stack)
        return out;
    for (int i = 0; i < stack->effects.size(); ++i) {
        const drift::Effect &effect = stack->effects.at(i);
        if (effect.catalogId != chromaKeyEffectId())
            continue;
        out.insert(QStringLiteral("present"), true);
        out.insert(QStringLiteral("enabled"), effect.enabled);
        out.insert(QStringLiteral("effectIndex"), i);
        break;
    }
    return out;
}

void AppController::toggleChromaKey(int trackIndex, int clipIndex)
{
    if (trackIndex < 0 && clipIndex < 0) {
        trackIndex = m_selectedTrack;
        clipIndex = m_selectedClip;
    }
    const QVariantMap info = chromaKeyInfo(trackIndex, clipIndex);
    if (!info.value(QStringLiteral("eligible")).toBool()) {
        setLastMessage(tr("Select a video or image clip to take out its green or blue background"),
                       QStringLiteral("warning"));
        return;
    }
    if (info.value(QStringLiteral("present")).toBool()) {
        int hostTrack = trackIndex;
        int hostClip = clipIndex;
        if (!redirectToEffectHost(&hostTrack, &hostClip, drift::AdjustmentKind::VideoEffects, /*create=*/false))
            return;
        const bool enable = !info.value(QStringLiteral("enabled")).toBool();
        setEffectEnabled(hostTrack, hostClip, info.value(QStringLiteral("effectIndex")).toInt(), enable);
        setLastMessage(enable ? tr("Chroma key on") : tr("Chroma key off"));
        return;
    }
    if (m_chromaDetecting)
        return;

    // Read the backdrop colour from the frame at the playhead first, so the key lands on it in one
    // undo step. The clip whose pixels are keyed is the media clip, also when its effects
    // adjustment is what is selected.
    const drift::Clip &target = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    const drift::Clip *media = &target;
    if (target.type == drift::ClipType::Adjustment) {
        int mt = -1;
        int mc = -1;
        media = !target.linkedClipId.isEmpty() && findClipById(m_project, target.linkedClipId, &mt, &mc)
                    ? &m_project.tracks().at(mt).clips.at(mc)
                    : nullptr;
    }
    QString path;
    drift::ClipType type = drift::ClipType::Video;
    drift::TimeUs sourceUs = 0;
    int rotation = 0;
    if (media && (media->type == drift::ClipType::Video || media->type == drift::ClipType::Image)
        && !media->path.isEmpty() && media->timelineDuration > 0) {
        const drift::TimeUs at = qBound(media->timelineStart, m_playheadUs, media->timelineEnd() - 1);
        path = media->path;
        type = media->type;
        sourceUs = media->timelineToSourceUs(at);
        rotation = media->rotationCorrection;
    }
    const QString targetId = target.id;
    m_chromaDetecting = true;
    (void)QtConcurrent::run([this, targetId, path, type, sourceUs, rotation]() {
        const double hue = path.isEmpty() ? std::numeric_limits<double>::quiet_NaN()
                                          : dominantBackdropHue(chromaAnalysisFrame(path, type, sourceUs, rotation));
        QMetaObject::invokeMethod(
            this,
            [this, targetId, hue]() {
                m_chromaDetecting = false;
                addChromaKey(targetId, hue);
            },
            Qt::QueuedConnection);
    });
}

void AppController::addChromaKey(const QString &clipId, double hue)
{
    int trackIndex = -1;
    int clipIndex = -1;
    if (!findClipById(m_project, clipId, &trackIndex, &clipIndex))
        return;
    const EffectPresetEntry *def = effectDefForId(chromaKeyEffectId());
    if (!def) {
        setLastMessage(tr("The Chroma Key effect is missing from this installation"), QStringLiteral("error"));
        return;
    }
    QString why;
    if (!effectFitsTrack(trackIndex, chromaKeyEffectId(), &why)) {
        setLastMessage(why, QStringLiteral("warning"));
        return;
    }

    drift::Effect effect;
    effect.name = def->filterName;
    effect.catalogId = def->meta.id;
    for (auto it = def->fixedParams.constBegin(); it != def->fixedParams.constEnd(); ++it)
        effect.parameters.insert(it.key(), it.value());
    for (const drift::EffectParamSpec &p : def->meta.parameters)
        p.insertDefault(effect.parameters);
    const bool detected = std::isfinite(hue);
    if (detected)
        effect.parameters.insert(QStringLiteral("u_keyHue"), hue);

    m_project.ensureTrackIds();
    const drift::Project before = m_project;
    int hostTrack = trackIndex;
    int hostClip = clipIndex;
    if (!redirectToEffectHost(&hostTrack, &hostClip, drift::AdjustmentKind::VideoEffects, /*create=*/true))
        return;
    m_project.tracks()[hostTrack].clips[hostClip].effects.append(effect);
    // The media clip stays selected: its Video tab shows the key's settings right under the Chroma
    // Key switch (Main.qml opens it on chromaKeyAdded). Keyed from its effects adjustment, that
    // adjustment stays selected and the Effects tab shows them.
    pushProjectEdit(before, tr("Chroma key"));
    finishEdit(tr("Chroma key"));

    const bool onMedia = !(trackIndex == hostTrack && clipIndex == hostClip);
    const bool green = detected && hue >= 70.0 && hue <= 170.0;
    const bool blue = detected && hue >= 180.0 && hue <= 260.0;
    if (onMedia) {
        setLastMessage(green  ? tr("Chroma key on: green screen found. Fine-tune it under Video › Chroma Key.")
                       : blue ? tr("Chroma key on: blue screen found. Fine-tune it under Video › Chroma Key.")
                       : detected
                           ? tr("Chroma key on, keyed on the frame's main colour. Change the colour under Video › Chroma Key if needed.")
                           : tr("Chroma key on. Pick the backdrop with Select Color under Video › Chroma Key."));
    } else {
        setLastMessage(green  ? tr("Chroma key on: green screen found. Fine-tune Tolerance and Edge Softness in Effects.")
                       : blue ? tr("Chroma key on: blue screen found. Fine-tune Tolerance and Edge Softness in Effects.")
                       : detected
                           ? tr("Chroma key on, keyed on the frame's main colour. Change Key Colour in Effects if needed.")
                           : tr("Chroma key on. Pick the backdrop with Key Colour's eyedropper in Effects."));
    }
    emit chromaKeyAdded();
}

void AppController::setChromaKeyOn(int trackIndex, int clipIndex, bool on)
{
    const QVariantMap info = chromaKeyInfo(trackIndex, clipIndex);
    if (!info.value(QStringLiteral("eligible")).toBool())
        return;
    const bool present = info.value(QStringLiteral("present")).toBool();
    if (!present) {
        if (on)
            toggleChromaKey(trackIndex, clipIndex); // reads the backdrop colour, then adds the key
        return;
    }
    if (info.value(QStringLiteral("enabled")).toBool() == on)
        return;
    int hostTrack = trackIndex;
    int hostClip = clipIndex;
    if (!redirectToEffectHost(&hostTrack, &hostClip, drift::AdjustmentKind::VideoEffects, /*create=*/false))
        return;
    setEffectEnabled(hostTrack, hostClip, info.value(QStringLiteral("effectIndex")).toInt(), on);
    setLastMessage(on ? tr("Chroma key on") : tr("Chroma key off"));
}

void AppController::resetChromaKey(int trackIndex, int clipIndex)
{
    const QVariantMap info = chromaKeyInfo(trackIndex, clipIndex);
    if (!info.value(QStringLiteral("present")).toBool())
        return;
    const EffectPresetEntry *def = effectDefForId(chromaKeyEffectId());
    if (!def)
        return;
    int hostTrack = trackIndex;
    int hostClip = clipIndex;
    if (!redirectToEffectHost(&hostTrack, &hostClip, drift::AdjustmentKind::VideoEffects, /*create=*/false))
        return;
    const int effectIndex = info.value(QStringLiteral("effectIndex")).toInt();
    const drift::Project before = m_project;
    drift::Clip &host = m_project.tracks()[hostTrack].clips[hostClip];
    if (effectIndex < 0 || effectIndex >= host.effects.size())
        return;
    drift::Effect &effect = host.effects[effectIndex];
    // Everything but the picked colour, which is what the key is for.
    for (const drift::EffectParamSpec &p : def->meta.parameters) {
        if (p.key == QLatin1String("u_keyHue"))
            continue;
        p.insertDefault(effect.parameters);
        effect.paramKeyframes.remove(p.key);
    }
    pushProjectEdit(before, tr("Reset chroma key"));
    finishEdit(tr("Chroma key settings reset"));
}

void AppController::resetVideoBasics(int trackIndex, int clipIndex)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;
    m_undoStack.beginMacro(tr("Reset video"));
    resetClipTransform(trackIndex, clipIndex);
    resetChromaKey(trackIndex, clipIndex);
    m_undoStack.endMacro();
    setLastMessage(tr("Video settings reset"));
}

// ---- Stacking (Dafin) -------------------------------------------------------------------------
//
// Drift places a clip that lands on a busy stretch of a track after whatever is there. That made
// it impossible to put music under a voice-over or a sound effect over it: the second sound was
// always pushed past the first. In Dafin a clip aimed at a busy stretch goes on the nearest track
// that has room — below for sound, above for pictures, as Filmora numbers them — and a new track
// is made right next to the one aimed at when none has. Overlapping only the edge of a neighbour
// (less than a fifth of the clip, and under a second) still slides it to the gap, which is what a
// slightly-off drop next to a clip means.

namespace {

bool spanBusy(const drift::Track &track, drift::TimeUs start, drift::TimeUs duration,
              const QSet<QString> &ignore)
{
    const drift::TimeUs end = start + qMax<drift::TimeUs>(1, duration);
    const drift::TimeUs graze = qMin<drift::TimeUs>(duration / 5, drift::secondsToUs(1.0));
    for (const drift::Clip &c : track.clips) {
        if (ignore.contains(c.id))
            continue;
        const drift::TimeUs overlap = qMin(end, c.timelineEnd()) - qMax(start, c.timelineStart);
        if (overlap > graze)
            return true;
    }
    return false;
}

bool stackableTrack(const drift::Track &track, drift::ClipType type)
{
    return !track.isAdjustment() && !track.locked && track.allowsClipType(type);
}

} // namespace

AppController::StackPlan AppController::stackPlan(int trackIndex, drift::ClipType type, drift::TimeUs start,
                                                  drift::TimeUs duration, const QSet<QString> &ignoreClipIds) const
{
    StackPlan plan;
    plan.track = trackIndex;
    const QList<drift::Track> &tracks = m_project.tracks();
    if (trackIndex < 0 || trackIndex >= tracks.size() || m_allowClipOverlap)
        return plan;
    const drift::Track &target = tracks.at(trackIndex);
    if (target.isAdjustment() || !target.allowsClipType(type))
        return plan;
    // Snapped the way the placement will snap it, so a drop that snaps to a clip's end counts as
    // landing beside it.
    const drift::TimeUs snapped = qMax<drift::TimeUs>(0, drift::snapTime(m_project, start, m_snapEnabled, m_playheadUs));
    if (!spanBusy(target, snapped, duration, ignoreClipIds))
        return plan;

    const bool sound = type == drift::ClipType::Audio;
    if (sound) {
        for (int t = trackIndex + 1; t < tracks.size(); ++t) {
            if (stackableTrack(tracks.at(t), type) && !spanBusy(tracks.at(t), snapped, duration, ignoreClipIds)) {
                plan.track = t;
                return plan;
            }
        }
        plan.track = -1;
        plan.insertIndex = trackIndex + 1;
        return plan;
    }
    for (int t = trackIndex - 1; t >= 0; --t) {
        const drift::Track &candidate = tracks.at(t);
        if (candidate.isAdjustmentLane())
            continue;
        if (stackableTrack(candidate, type) && !spanBusy(candidate, snapped, duration, ignoreClipIds)) {
            plan.track = t;
            return plan;
        }
    }
    plan.track = -1;
    // Directly above the track aimed at, under the nested lanes that sit on top of it.
    int at = trackIndex;
    while (at > 0 && tracks.at(at - 1).isAdjustmentLane()
           && tracks.at(at - 1).parentTrackId == target.id)
        --at;
    plan.insertIndex = at;
    return plan;
}

QVariantMap AppController::stackTargetForKind(int trackIndex, const QString &mediaKind, double startSeconds,
                                              double durationSeconds) const
{
    const StackPlan plan = stackPlan(trackIndex, drift::clipTypeFromString(mediaKind),
                                     drift::secondsToUs(qMax(0.0, startSeconds)),
                                     drift::secondsToUs(qMax(0.04, durationSeconds)));
    if (plan.insertIndex >= 0)
        return {{QStringLiteral("newTrack"), true}, {QStringLiteral("insertIndex"), plan.insertIndex}};
    return {{QStringLiteral("newTrack"), false}, {QStringLiteral("track"), plan.track}};
}

QVariantMap AppController::stackTargetForClip(int fromTrack, int clipIndex, int targetTrack, double startSeconds) const
{
    if (!isValidClipIndex(fromTrack, clipIndex))
        return {{QStringLiteral("newTrack"), false}, {QStringLiteral("track"), targetTrack}};
    const drift::Clip &clip = m_project.tracks().at(fromTrack).clips.at(clipIndex);
    // The clip and whatever moves with it are not in their own way.
    QSet<QString> ignore{clip.id};
    for (const QPair<int, int> &pair : m_selection) {
        if (isValidClipIndex(pair.first, pair.second))
            ignore.insert(m_project.tracks().at(pair.first).clips.at(pair.second).id);
    }
    for (const drift::ClipRef &ref : drift::linkedPartners(m_project, clip))
        ignore.insert(m_project.tracks().at(ref.trackIndex).clips.at(ref.clipIndex).id);
    const StackPlan plan = stackPlan(targetTrack, clip.type, drift::secondsToUs(qMax(0.0, startSeconds)),
                                     clip.timelineDuration, ignore);
    if (plan.insertIndex >= 0)
        return {{QStringLiteral("newTrack"), true}, {QStringLiteral("insertIndex"), plan.insertIndex}};
    return {{QStringLiteral("newTrack"), false}, {QStringLiteral("track"), plan.track}};
}

void AppController::moveClipToNewTrack(int trackIndex, int clipIndex, int insertIndex, double newStart)
{
    if (!isValidClipIndex(trackIndex, clipIndex) || m_moveBaseline)
        return;
    const drift::Clip clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    if (clip.type == drift::ClipType::Adjustment) {
        // An adjustment layer on its own track applies to everything below it, so dragging it onto
        // a track edge moves it onto a new adjustment track there: that is how you choose what it
        // changes, as in Filmora. Lanes (scoped to one track) and the transform, camera and audio
        // kinds keep Drift's own moves.
        const drift::Track &from = m_project.tracks().at(trackIndex);
        if (!from.isAdjustment() || from.isAdjustmentLane() || from.isTransformLayer()
            || from.isCameraLayer()
            || (clip.adjustmentKind != drift::AdjustmentKind::VideoEffects
                && clip.adjustmentKind != drift::AdjustmentKind::Mask)) {
            return;
        }
        const int trackCount = int(m_project.tracks().size());
        int at = qBound(0, insertIndex, trackCount);
        // Below a track means below its nested lanes too, which are stored under it.
        while (at < trackCount && m_project.tracks().at(at).isAdjustmentLane())
            ++at;
        // Alone on its track and dropped on that track's own edge: the stack is unchanged.
        if (from.clips.size() == 1 && (at == trackIndex || at == trackIndex + 1)) {
            moveClip(trackIndex, clipIndex, newStart);
            return;
        }

        const drift::Project before = m_project;
        m_project.ensureTrackIds();
        const QString sourceTrackId = m_project.tracks().at(trackIndex).id;
        drift::Clip moved = clip;
        // Standalone means "everything below", which is not something a clip can be pinned to.
        moved.linkedClipId.clear();
        moved.timelineStart = drift::secondsToUs(qMax(0.0, newStart));

        drift::Track track;
        track.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        track.type = drift::TrackType::Adjustment;
        track.adjustmentScope = drift::AdjustmentScope::AllBelow;
        track.clips.append(moved);
        m_project.tracks().insert(at, track);

        const int sourceIndex = m_project.trackIndexById(sourceTrackId);
        if (sourceIndex >= 0) {
            m_project.tracks()[sourceIndex].clips.removeAt(clipIndex);
            // A track that only held this layer would be left as an empty row.
            if (m_project.tracks().at(sourceIndex).clips.isEmpty())
                m_project.tracks().removeAt(sourceIndex);
        }
        normalizeAdjustmentLanes(m_project);
        clearSelection();
        pushProjectEdit(before, tr("Move adjustment layer to a new track"));
        finishEdit(tr("Adjustment layer moved"));
        int t = -1;
        int c = -1;
        if (findClipById(m_project, moved.id, &t, &c))
            selectClip(t, c);
        return;
    }
    const drift::Project before = m_project;
    const int at = qBound(0, insertIndex, int(m_project.tracks().size()));
    const int newTrack = drift::insertTrackAboveForClipType(m_project, at, clip.type);
    if (!m_project.tracks().at(newTrack).acceptsClip(clip)) {
        m_project = before;
        return;
    }
    // Indices at and below the new track moved down one.
    const auto shifted = [newTrack](int t) { return t >= newTrack ? t + 1 : t; };
    for (QPair<int, int> &pair : m_selection)
        pair.first = shifted(pair.first);
    if (m_selectedTrack >= 0)
        m_selectedTrack = shifted(m_selectedTrack);
    m_moveBaseline = before;
    moveClipToTrack(shifted(trackIndex), clipIndex, newTrack, newStart);
    m_moveBaseline.reset();

    int t = -1;
    int c = -1;
    if (!findClipById(m_project, clip.id, &t, &c) || m_project.tracks().at(newTrack).clips.isEmpty()) {
        // The move was refused (a clip can't cover another completely, …): no empty track left behind.
        if (m_project.tracks().size() > newTrack && m_project.tracks().at(newTrack).clips.isEmpty()
            && m_project.tracks().size() == before.tracks().size() + 1) {
            m_project.tracks().removeAt(newTrack);
            for (QPair<int, int> &pair : m_selection)
                if (pair.first > newTrack)
                    --pair.first;
            if (m_selectedTrack > newTrack)
                --m_selectedTrack;
            finishEdit(QString());
        }
    }
}

// ---------------------------------------------------------------------------
// Split screen templates (Templates tab > Split Screen)

namespace {

struct SplitCell
{
    double x, y, w, h;                    // the cell's box, as fractions of the canvas
    std::array<QPointF, 4> shape;         // corners within the box (0..1); a plain box is the unit square
    bool slanted = false;                 // true when `shape` is not the box itself
};

struct SplitLayout
{
    const char *id;
    const char *label;
    std::vector<SplitCell> cells;
};

SplitCell box(double x, double y, double w, double h)
{
    return {x, y, w, h, {QPointF(0, 0), QPointF(1, 0), QPointF(1, 1), QPointF(0, 1)}, false};
}

// The layouts of Filmora's Split Screen category, in its order, then a few plain extras.
const std::vector<SplitLayout> &splitLayouts()
{
    static const std::vector<SplitLayout> layouts = {
        {"rows3", "3 rows", {box(0, 0, 1, 1.0 / 3), box(0, 1.0 / 3, 1, 1.0 / 3), box(0, 2.0 / 3, 1, 1.0 / 3)}},
        {"diagonal2",
         "Diagonal",
         {{0, 0, 0.58, 1, {QPointF(0, 0), QPointF(1, 0), QPointF(0.72, 1), QPointF(0, 1)}, true},
          {0.42, 0, 0.58, 1, {QPointF(0.28, 0), QPointF(1, 0), QPointF(1, 1), QPointF(0, 1)}, true}}},
        {"grid2x2", "2 x 2", {box(0, 0, 0.5, 0.5), box(0.5, 0, 0.5, 0.5), box(0, 0.5, 0.5, 0.5), box(0.5, 0.5, 0.5, 0.5)}},
        {"cols4", "4 columns", {box(0, 0, 0.25, 1), box(0.25, 0, 0.25, 1), box(0.5, 0, 0.25, 1), box(0.75, 0, 0.25, 1)}},
        {"top3bottom1",
         "3 over 1",
         {box(0, 0, 1.0 / 3, 0.3), box(1.0 / 3, 0, 1.0 / 3, 0.3), box(2.0 / 3, 0, 1.0 / 3, 0.3), box(0, 0.3, 1, 0.7)}},
        {"top1bottom3",
         "1 over 3",
         {box(0, 0, 1, 0.7), box(0, 0.7, 1.0 / 3, 0.3), box(1.0 / 3, 0.7, 1.0 / 3, 0.3), box(2.0 / 3, 0.7, 1.0 / 3, 0.3)}},
        {"leftright", "Left and right", {box(0, 0, 0.5, 1), box(0.5, 0, 0.5, 1)}},
        {"topbottom", "Top and bottom", {box(0, 0, 1, 0.5), box(0, 0.5, 1, 0.5)}},
        {"cols3", "3 columns", {box(0, 0, 1.0 / 3, 1), box(1.0 / 3, 0, 1.0 / 3, 1), box(2.0 / 3, 0, 1.0 / 3, 1)}},
        {"left1right2", "1 and 2", {box(0, 0, 0.5, 1), box(0.5, 0, 0.5, 0.5), box(0.5, 0.5, 0.5, 0.5)}},
        {"grid3x3",
         "3 x 3",
         {box(0, 0, 1.0 / 3, 1.0 / 3), box(1.0 / 3, 0, 1.0 / 3, 1.0 / 3), box(2.0 / 3, 0, 1.0 / 3, 1.0 / 3),
          box(0, 1.0 / 3, 1.0 / 3, 1.0 / 3), box(1.0 / 3, 1.0 / 3, 1.0 / 3, 1.0 / 3),
          box(2.0 / 3, 1.0 / 3, 1.0 / 3, 1.0 / 3), box(0, 2.0 / 3, 1.0 / 3, 1.0 / 3),
          box(1.0 / 3, 2.0 / 3, 1.0 / 3, 1.0 / 3), box(2.0 / 3, 2.0 / 3, 1.0 / 3, 1.0 / 3)}},
    };
    return layouts;
}

} // namespace

QVariantList AppController::splitScreenLayouts() const
{
    QVariantList out;
    for (const SplitLayout &layout : splitLayouts()) {
        QVariantList cells;
        for (const SplitCell &cell : layout.cells) {
            QVariantList corners;
            for (const QPointF &p : cell.shape)
                corners.append(QVariantMap{{QStringLiteral("x"), p.x()}, {QStringLiteral("y"), p.y()}});
            cells.append(QVariantMap{{QStringLiteral("x"), cell.x}, {QStringLiteral("y"), cell.y},
                                     {QStringLiteral("w"), cell.w}, {QStringLiteral("h"), cell.h},
                                     {QStringLiteral("corners"), corners}});
        }
        out.append(QVariantMap{{QStringLiteral("id"), QString::fromLatin1(layout.id)},
                               {QStringLiteral("label"), tr(layout.label)},
                               {QStringLiteral("cells"), cells}});
    }
    return out;
}

int AppController::applySplitScreen(const QString &layoutId)
{
    const SplitLayout *layout = nullptr;
    for (const SplitLayout &candidate : splitLayouts()) {
        if (layoutId == QLatin1String(candidate.id))
            layout = &candidate;
    }
    if (!layout)
        return 0;

    const auto isPicture = [](const drift::Clip &clip) {
        return (clip.type == drift::ClipType::Video || clip.type == drift::ClipType::Image) && !clip.id.isEmpty();
    };

    // The clips to place, top track first: the selection when it holds two or more pictures,
    // otherwise every picture under the playhead.
    QList<QPair<int, int>> refs;
    for (const auto &pair : m_selection) {
        if (pair.first >= 0 && pair.first < m_project.tracks().size() && pair.second >= 0
            && pair.second < m_project.tracks().at(pair.first).clips.size()
            && isPicture(m_project.tracks().at(pair.first).clips.at(pair.second)))
            refs.append(pair);
    }
    if (refs.size() < 2) {
        refs.clear();
        for (int t = 0; t < m_project.tracks().size(); ++t) {
            const drift::Track &track = m_project.tracks().at(t);
            if (track.hidden)
                continue;
            for (int c = 0; c < track.clips.size(); ++c) {
                if (isPicture(track.clips.at(c)) && track.clips.at(c).containsTime(m_playheadUs))
                    refs.append({t, c});
            }
        }
    }
    if (refs.isEmpty()) {
        setLastMessage(tr("Put clips on different video tracks, or select them, then pick a split screen"),
                       QStringLiteral("warning"));
        return 0;
    }
    std::sort(refs.begin(), refs.end());

    // Indices shift when a mask lane is inserted, so the clips are followed by id.
    QStringList ids;
    for (const auto &pair : refs)
        ids.append(m_project.tracks().at(pair.first).clips.at(pair.second).id);

    const double canvasW = qMax(1, m_project.width());
    const double canvasH = qMax(1, m_project.height());
    const drift::Project before = m_project;
    int placed = 0;
    for (int i = 0; i < ids.size() && i < static_cast<int>(layout->cells.size()); ++i) {
        int t = -1;
        int c = -1;
        if (!findClipById(m_project, ids.at(i), &t, &c))
            continue;
        drift::Clip &clip = m_project.tracks()[t].clips[c];
        const SplitCell &cell = layout->cells[static_cast<size_t>(i)];

        // The picture's own shape: the asset's size with its rotation correction, else the shape
        // of the rect it is drawn in now.
        double mediaW = 0;
        double mediaH = 0;
        if (const drift::MediaAsset *asset = m_project.asset(clip.assetId)) {
            mediaW = asset->width;
            mediaH = asset->height;
            if (clip.rotationCorrection % 180 != 0)
                std::swap(mediaW, mediaH);
        }
        if (mediaW <= 0 || mediaH <= 0) {
            const drift::TimeUs rel = qMax<drift::TimeUs>(0, m_playheadUs - clip.timelineStart);
            mediaW = clip.transformW.isEmpty() ? canvasW : clip.transformW.evaluateAt(rel);
            mediaH = clip.transformH.isEmpty() ? canvasH : clip.transformH.evaluateAt(rel);
        }

        // Scale to cover the cell, centred on it, then cut the overflow away with a mask.
        const double cellX = cell.x * canvasW;
        const double cellY = cell.y * canvasH;
        const double cellW = cell.w * canvasW;
        const double cellH = cell.h * canvasH;
        const double scale = qMax(cellW / mediaW, cellH / mediaH);
        const double w = mediaW * scale;
        const double h = mediaH * scale;
        const double x = cellX + (cellW - w) / 2.0;
        const double y = cellY + (cellH - h) / 2.0;
        drift::appdetail::setClipLayoutPixels(clip, x, y, w, h);
        clip.rotation = {};

        // Mask coordinates are fractions of the clip's own frame.
        drift::Mask mask;
        mask.shape = cell.slanted ? drift::MaskShape::Freeform : drift::MaskShape::Rectangle;
        mask.name = tr("Split screen");
        mask.w = cellW / w;
        mask.h = cellH / h;
        mask.x = (cellX + cellW / 2.0 - x) / w;
        mask.y = (cellY + cellH / 2.0 - y) / h;
        if (cell.slanted) {
            for (const QPointF &p : cell.shape)
                mask.points.append(QPointF((cellX + p.x() * cellW - x) / w, (cellY + p.y() * cellH - y) / h));
        }
        writeClipMask(t, c, mask);
        ++placed;
    }

    pushProjectEdit(before, tr("Split screen"));
    finishEdit(ids.size() > static_cast<int>(layout->cells.size())
                   ? tr("Split screen: %1 of %2 clips placed").arg(placed).arg(ids.size())
                   : tr("Split screen applied"));
    return placed;
}

// Dafin: "Clip deleted. Undo" toast. A click after another edit must not undo that edit instead.
bool AppController::undoIfAtIndex(int undoIndex)
{
    if (undoIndex <= 0 || m_undoStack.index() != undoIndex)
        return false;
    undo();
    return true;
}

// Dafin: "Remove all gaps" for one track (-1 = the selected track).
void AppController::removeAllGaps(int trackIndex)
{
    if (trackIndex < 0)
        trackIndex = m_selectedTrack;
    if (trackIndex < 0 || trackIndex >= m_project.tracks().size())
        return;

    // Snapshot first: QList is copy-on-write, see closeGap().
    const drift::Project before = m_project;
    drift::Track &track = m_project.tracks()[trackIndex];
    const QSet<QString> movedIds = drift::closeAllGapsInTrack(track);
    if (movedIds.isEmpty()) {
        setLastMessage(tr("No gaps on this track"), QStringLiteral("info"));
        return;
    }
    for (const drift::Clip &clip : track.clips) {
        if (movedIds.contains(clip.id))
            syncLinkedPartnersFrom(m_project, clip, movedIds);
    }
    pushProjectEdit(before, tr("Remove all gaps"));
    finishEdit(tr("Gaps removed"));
}
