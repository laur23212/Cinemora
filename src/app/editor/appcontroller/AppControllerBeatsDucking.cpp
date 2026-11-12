// Dafin: the timeline toolbar's Beat markers and Auto ducking buttons.
//
// Beat markers: select a music clip and press the button. Its beats are found locally (Drift's
// spectral-flux onsets + autocorrelation tempo, AudioOnsets, no cloud) from that clip alone and a
// bookmark is dropped on every beat ("♪ Bar n" on the first beat of a bar, "♪ Beat" on the
// others). Bookmarks already magnet clips, cuts and the playhead when snapping is on, so cuts line
// up with the music. Music without a steady tempo gets its strong hits instead. Press again with
// the clip selected to take the markers in its stretch away. One undo step each way.
//
// Auto ducking: select the music and press the button. Every other clip with sound that plays
// over it (voice-over, the camera's sound) is listened to, and where someone speaks the music
// goes down to a quarter of its level (about -12 dB), with a short ramp before and after. The
// dip is written as dots on the music's volume line (its "volume" keyframes), so each one can be
// dragged afterwards like any other dot. The button is a switch, off on every clip to begin
// with: it lights up while the selected music is ducked, and pressing it then takes the dips
// out again (every dot inside the stretches ducking wrote, also one dragged within them, and the
// line's end pins it added), leaving the rest of the volume line as it was. The stretches are
// saved with the clip (Clip::duckWindows) and move with it on trims and splits. One undo step.
//
// The planning (which beats, which spans count as speech, where the dots go) is in
// core/edit/BeatDuckPlan, tested by tst_beatduck.

#include "app/editor/appcontroller/AppControllerInternal.h"
#include "core/edit/BeatDuckPlan.h"
#include "engine/audio/AudioMixer.h"
#include "engine/audio/analysis/AudioOnsets.h"

#include <QSet>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>

using namespace drift::appdetail;
using namespace drift::appcontroller_detail;

namespace {

using drift::beatduck::BeatMarker;
using drift::beatduck::DuckKey;
using drift::beatduck::Span;

bool carriesSound(const drift::Clip &clip)
{
    return clip.timelineDuration > 0
           && (clip.type == drift::ClipType::Audio
               || (clip.type == drift::ClipType::Video && !clip.suppressEmbeddedAudio));
}

// A copy of the project in which only `keepIds` make a sound. The other clips are silenced
// rather than removed, so links, transitions and indices stay exactly as the mixer expects.
drift::Project soundOnly(const drift::Project &project, const QSet<QString> &keepIds)
{
    drift::Project snap = project;
    for (drift::Track &track : snap.tracks()) {
        for (drift::Clip &clip : track.clips) {
            if (keepIds.contains(clip.id))
                continue;
            clip.volume = drift::KeyframeTrack<double>();
            clip.volume.setKeyframe(0, 0.0);
        }
    }
    return snap;
}

// Loudness of the mixed sound in 50 ms windows, in dBFS.
QVector<float> levelEnvelope(const drift::Project &snap, drift::TimeUs startUs, drift::TimeUs durUs,
                             int rate, int hopFrames)
{
    QVector<float> levels;
    const qint64 frames = qint64(double(durUs) / double(drift::kUsPerSecond) * rate);
    if (frames <= 0)
        return levels;
    AudioMixer mixer;
    mixer.setProject(&snap);
    const int hops = int(frames / hopFrames);
    levels.reserve(hops);
    constexpr int kHopsPerChunk = 256;
    QVector<float> buffer(qsizetype(hopFrames) * kHopsPerChunk * 2);
    for (int h = 0; h < hops; h += kHopsPerChunk) {
        const int n = qMin(kHopsPerChunk, hops - h);
        const drift::TimeUs at = startUs + qint64(h) * hopFrames * drift::kUsPerSecond / rate;
        mixer.mix(at, n * hopFrames, rate, buffer.data());
        for (int j = 0; j < n; ++j) {
            double sum = 0.0;
            const float *p = buffer.constData() + qsizetype(j) * hopFrames * 2;
            for (int i = 0; i < hopFrames; ++i) {
                const double m = 0.5 * (p[i * 2] + p[i * 2 + 1]);
                sum += m * m;
            }
            const double rms = std::sqrt(sum / hopFrames);
            levels.append(float(20.0 * std::log10(qMax(rms, 1e-6))));
        }
    }
    return levels;
}

} // namespace

// ---- Beat markers -----------------------------------------------------------------------------

QVariantMap AppController::beatMarkersInfo(int trackIndex, int clipIndex) const
{
    QVariantMap info{{QStringLiteral("eligible"), false},
                     {QStringLiteral("count"), 0},
                     {QStringLiteral("busy"), m_beatMarkersBusy}};
    if (!isValidClipIndex(trackIndex, clipIndex))
        return info;
    const drift::Clip &clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    if (!carriesSound(clip))
        return info;
    int count = 0;
    for (const drift::Bookmark &b : m_project.bookmarks()) {
        if (b.timeUs >= clip.timelineStart && b.timeUs < clip.timelineEnd()
            && drift::beatduck::isBeatMarkerLabel(b.label))
            ++count;
    }
    info.insert(QStringLiteral("eligible"), true);
    info.insert(QStringLiteral("count"), count);
    return info;
}

void AppController::toggleBeatMarkers(int trackIndex, int clipIndex)
{
    if (trackIndex < 0 && clipIndex < 0) {
        trackIndex = m_selectedTrack;
        clipIndex = m_selectedClip;
    }
    if (m_beatMarkersBusy)
        return;
    const QVariantMap info = beatMarkersInfo(trackIndex, clipIndex);
    if (!info.value(QStringLiteral("eligible")).toBool()) {
        setLastMessage(tr("Select a music clip to mark its beats"), QStringLiteral("warning"));
        return;
    }
    const drift::Clip &clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    const drift::TimeUs startUs = clip.timelineStart;
    const drift::TimeUs endUs = clip.timelineEnd();

    if (info.value(QStringLiteral("count")).toInt() > 0) {
        const drift::Project before = m_project;
        QList<drift::Bookmark> &bookmarks = m_project.bookmarks();
        const qsizetype removed = bookmarks.removeIf([&](const drift::Bookmark &b) {
            return b.timeUs >= startUs && b.timeUs < endUs && drift::beatduck::isBeatMarkerLabel(b.label);
        });
        pushProjectEdit(before, tr("Remove beat markers"));
        finishEdit(tr("%n beat marker(s) removed", nullptr, int(removed)));
        return;
    }

    const double startSeconds = drift::usToSeconds(startUs);
    const double durSeconds = drift::usToSeconds(endUs - startUs);
    if (durSeconds < AudioOnsets::kMinAnalysisSec) {
        setLastMessage(tr("The clip is too short to find a beat — it needs at least %1 seconds")
                           .arg(AudioOnsets::kMinAnalysisSec),
                       QStringLiteral("warning"));
        return;
    }

    const QString clipId = clip.id;
    const drift::Project snap = soundOnly(m_project, {clipId});
    m_beatMarkersBusy = true;
    emit beatMarkersBusyChanged();
    setLastMessage(tr("Finding the beats…"));
    (void)QtConcurrent::run([this, snap, startUs, endUs, startSeconds, clipId] {
        const AudioBeatAnalysis analysis =
            runBeatAnalysis(snap, startUs, endUs - startUs, startSeconds);
        QMetaObject::invokeMethod(
            this,
            [this, analysis, startUs, endUs, clipId] {
                m_beatMarkersBusy = false;
                emit beatMarkersBusyChanged();
                applyBeatMarkers(clipId, analysis, startUs, endUs);
            },
            Qt::QueuedConnection);
    });
}

void AppController::applyBeatMarkers(const QString &clipId, const AudioBeatAnalysis &analysis,
                                     drift::TimeUs startUs, drift::TimeUs endUs)
{
    int tr_ = -1;
    int cl = -1;
    if (!findClipById(m_project, clipId, &tr_, &cl))
        return; // the clip went away while we listened
    // The clip may have moved meanwhile: shift the analysis with it.
    const drift::Clip &clip = m_project.tracks().at(tr_).clips.at(cl);
    const double shift = drift::usToSeconds(clip.timelineStart - startUs);

    QList<drift::beatduck::OnsetIn> onsets;
    for (const AudioOnset &o : analysis.onsets)
        onsets.append({o.seconds, o.strength});
    const QList<BeatMarker> markers = drift::beatduck::planBeatMarkers(
        analysis.beats, analysis.beatsPerBar, analysis.firstDownbeat, onsets,
        drift::usToSeconds(startUs), drift::usToSeconds(endUs));
    if (markers.isEmpty()) {
        setLastMessage(tr("No beat found in this clip"), QStringLiteral("warning"));
        return;
    }

    const drift::Project before = m_project;
    QList<drift::Bookmark> &bookmarks = m_project.bookmarks();
    for (const BeatMarker &m : markers) {
        bookmarks.append({.timeUs = qMax<drift::TimeUs>(0, drift::secondsToUs(m.seconds + shift)),
                          .label = drift::beatduck::beatMarkerLabel(m)});
    }
    pushProjectEdit(before, tr("Add beat markers"));
    finishEdit(analysis.bpm > 0.0
                   ? tr("%n beat marker(s) added · %1 BPM", nullptr, int(markers.size()))
                         .arg(qRound(analysis.bpm))
                   : tr("%n beat marker(s) added on the strong hits (no steady tempo)", nullptr,
                        int(markers.size())));
}

// ---- Auto ducking -----------------------------------------------------------------------------

QVariantMap AppController::autoDuckInfo(int trackIndex, int clipIndex) const
{
    QVariantMap info{{QStringLiteral("eligible"), false},
                     {QStringLiteral("on"), false},
                     {QStringLiteral("busy"), m_autoDuckBusy}};
    if (!isValidClipIndex(trackIndex, clipIndex))
        return info;
    const drift::Clip &clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    info.insert(QStringLiteral("eligible"), carriesSound(clip));
    info.insert(QStringLiteral("on"), !clip.duckWindows.isEmpty());
    return info;
}

void AppController::toggleAutoDuck(int trackIndex, int clipIndex)
{
    if (trackIndex < 0 && clipIndex < 0) {
        trackIndex = m_selectedTrack;
        clipIndex = m_selectedClip;
    }
    if (autoDuckInfo(trackIndex, clipIndex).value(QStringLiteral("on")).toBool())
        removeAutoDuck(trackIndex, clipIndex);
    else
        autoDuckMusic(trackIndex, clipIndex);
}

void AppController::removeAutoDuck(int trackIndex, int clipIndex)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;
    const drift::Project before = m_project;
    drift::Clip &music = m_project.tracks()[trackIndex].clips[clipIndex];
    // Every dot inside a stretch ducking wrote goes, also one that was dragged within it; the line
    // outside those stretches is left exactly as it is.
    constexpr drift::TimeUs kSlackUs = 1000;
    QList<drift::TimeUs> doomed;
    for (auto it = music.volume.keyframes().constBegin(); it != music.volume.keyframes().constEnd(); ++it) {
        for (const auto &w : std::as_const(music.duckWindows)) {
            if (it.key() >= w.first - kSlackUs && it.key() <= w.second + kSlackUs) {
                doomed.append(it.key());
                break;
            }
        }
    }
    if (music.duckPins & 1)
        doomed.append(0);
    if (music.duckPins & 2)
        doomed.append(music.timelineDuration);
    for (const drift::TimeUs rel : std::as_const(doomed))
        music.volume.removeKeyframe(rel);
    music.duckWindows.clear();
    music.duckPins = 0;
    pushProjectEdit(before, tr("Auto ducking off"));
    finishEdit(tr("Auto ducking off — the music plays at its own level again"));
}

void AppController::autoDuckMusic(int trackIndex, int clipIndex)
{
    if (trackIndex < 0 && clipIndex < 0) {
        trackIndex = m_selectedTrack;
        clipIndex = m_selectedClip;
    }
    if (m_autoDuckBusy)
        return;
    if (!autoDuckInfo(trackIndex, clipIndex).value(QStringLiteral("eligible")).toBool()) {
        setLastMessage(tr("Select the music to lower it while someone speaks"),
                       QStringLiteral("warning"));
        return;
    }
    const drift::Clip &music = m_project.tracks().at(trackIndex).clips.at(clipIndex);

    // The voices: every other clip with sound that plays over the music, on another unmuted track.
    // Selected clips on the music's own track count as music too and are never listened to.
    QSet<QString> musicIds{music.id};
    for (const auto &sel : std::as_const(m_selection)) {
        if (sel.first == trackIndex && isValidClipIndex(sel.first, sel.second))
            musicIds.insert(m_project.tracks().at(sel.first).clips.at(sel.second).id);
    }
    QSet<QString> voiceIds;
    for (int t = 0; t < m_project.tracks().size(); ++t) {
        const drift::Track &track = m_project.tracks().at(t);
        if (t == trackIndex || track.muted)
            continue;
        if (track.type != drift::TrackType::Audio && track.type != drift::TrackType::Video)
            continue;
        for (const drift::Clip &c : track.clips) {
            if (carriesSound(c) && !musicIds.contains(c.id) && c.timelineStart < music.timelineEnd()
                && c.timelineEnd() > music.timelineStart)
                voiceIds.insert(c.id);
        }
    }
    if (voiceIds.isEmpty()) {
        setLastMessage(tr("Nothing with sound plays over this music — put the voice on another track"),
                       QStringLiteral("warning"));
        return;
    }

    const QString musicId = music.id;
    const drift::TimeUs startUs = music.timelineStart;
    const drift::TimeUs durUs = music.timelineDuration;
    const drift::Project snap = soundOnly(m_project, voiceIds);
    m_autoDuckBusy = true;
    emit autoDuckBusyChanged();
    setLastMessage(tr("Listening for speech…"));
    (void)QtConcurrent::run([this, snap, startUs, durUs, musicId] {
        constexpr int kRate = 22050;
        constexpr int kHop = 1103; // ≈ 50 ms
        const QVector<float> levels = levelEnvelope(snap, startUs, durUs, kRate, kHop);
        const QList<Span> speech = drift::beatduck::speechSpans(
            levels, double(kHop) / kRate, drift::usToSeconds(startUs));
        QMetaObject::invokeMethod(
            this,
            [this, musicId, speech, startUs] {
                m_autoDuckBusy = false;
                emit autoDuckBusyChanged();
                applyAutoDuck(musicId, speech, startUs);
            },
            Qt::QueuedConnection);
    });
}

void AppController::applyAutoDuck(const QString &musicId, const QList<drift::beatduck::Span> &speechIn,
                                  drift::TimeUs analysedStartUs)
{
    int trackIndex = -1;
    int clipIndex = -1;
    if (!findClipById(m_project, musicId, &trackIndex, &clipIndex))
        return;
    if (speechIn.isEmpty()) {
        setLastMessage(tr("No speech found over this music"), QStringLiteral("warning"));
        return;
    }
    drift::Clip &music = m_project.tracks()[trackIndex].clips[clipIndex];
    const double shift = drift::usToSeconds(music.timelineStart - analysedStartUs);
    QList<Span> speech = speechIn;
    for (Span &s : speech) {
        s.start += shift;
        s.end += shift;
    }
    const double clipStart = drift::usToSeconds(music.timelineStart);
    const double clipEnd = drift::usToSeconds(music.timelineEnd());
    const drift::beatduck::DuckSettings settings;
    const QList<DuckKey> keys = drift::beatduck::planDuckKeys(speech, clipStart, clipEnd, settings);
    if (keys.isEmpty()) {
        setLastMessage(tr("No speech found over this music"), QStringLiteral("warning"));
        return;
    }
    const QList<Span> windows = drift::beatduck::duckWindows(keys);

    const drift::Project before = m_project;
    const bool wasFlat = music.volume.keyframes().size() < 2;
    const double flatGain = music.volume.isEmpty() ? 1.0 : music.volume.keyframes().first().value;

    // Clear the dots an earlier pass (or anything else) left inside the windows this pass owns, so
    // pressing twice leaves the same line as pressing once.
    QList<drift::TimeUs> doomed;
    for (auto it = music.volume.keyframes().constBegin(); it != music.volume.keyframes().constEnd(); ++it) {
        const double at = clipStart + drift::usToSeconds(it.key());
        for (const Span &w : windows) {
            if (at >= w.start - 1e-6 && at <= w.end + 1e-6) {
                doomed.append(it.key());
                break;
            }
        }
    }
    for (const drift::TimeUs rel : std::as_const(doomed))
        music.volume.removeKeyframe(rel);

    // The music's own level at each dot, read once the old dips are gone, so a shaped line keeps
    // its shape. A dip's ducked level follows the level the music had going into it.
    const drift::TimeUs durUs = music.timelineDuration;
    const auto relOf = [&](double seconds) {
        return qBound<drift::TimeUs>(0, drift::secondsToUs(seconds) - music.timelineStart, durUs);
    };
    const auto levelAt = [&](drift::TimeUs rel) {
        if (music.volume.isEmpty())
            return 1.0;
        return music.volume.enabled() ? music.volume.evaluateAt(rel)
                                      : music.volume.keyframes().first().value;
    };
    QList<double> levels;
    double dipRest = 1.0;
    for (int i = 0, w = 0; i < keys.size(); ++i) {
        const double here = levelAt(relOf(keys.at(i).seconds));
        while (w < windows.size() && keys.at(i).seconds > windows.at(w).end + 1e-9)
            ++w;
        if (w < windows.size() && std::abs(keys.at(i).seconds - windows.at(w).start) < 1e-9)
            dipRest = here; // the dip's first dot
        levels.append(keys.at(i).ducked ? dipRest * settings.amount : here);
    }

    // A flat line gets its two ends pinned first, as the volume line's first click does, so the
    // dips are bumps in the line rather than a new level for the whole clip.
    int pins = music.duckPins;
    if (wasFlat) {
        if (!music.volume.keyframes().contains(0)) {
            writeClipPropValue(music, QStringLiteral("volume"), 0, flatGain, true, true);
            pins |= 1;
        }
        if (!music.volume.keyframes().contains(durUs)) {
            writeClipPropValue(music, QStringLiteral("volume"), durUs, flatGain, true, true);
            pins |= 2;
        }
    }
    for (int i = 0; i < keys.size(); ++i) {
        writeClipPropValue(music, QStringLiteral("volume"), relOf(keys.at(i).seconds), levels.at(i),
                           true, true);
    }
    music.volume.setEnabled(true);
    // Remember what ducking owns, so the button can switch it off again.
    for (const Span &w : windows)
        music.duckWindows.append({relOf(w.start), relOf(w.end)});
    music.duckPins = pins;

    pushProjectEdit(before, tr("Auto ducking"));
    finishEdit(tr("Music lowered under %n stretch(es) of speech — drag the dots on its volume line to adjust",
                  nullptr, int(windows.size())));
}
