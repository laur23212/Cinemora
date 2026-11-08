#include "core/interchange/ImportedTimelineProject.h"

#include "core/project/Track.h"
#include "core/project/clip/Clip.h"
#include "core/project/media/MediaAsset.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QUuid>

#include <cmath>

namespace drift::interchange {

namespace {

QString tr(const char *text, const char *disambiguation = nullptr, int n = -1)
{
    return QCoreApplication::translate("ProjectImport", text, disambiguation, n);
}

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool isAudioSuffix(const QString &path)
{
    static const QStringList audio{QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("aac"),
                                   QStringLiteral("m4a"), QStringLiteral("flac"), QStringLiteral("ogg"),
                                   QStringLiteral("oga"), QStringLiteral("opus"), QStringLiteral("wma"),
                                   QStringLiteral("aif"), QStringLiteral("aiff"), QStringLiteral("caf"),
                                   QStringLiteral("ac3"), QStringLiteral("amr")};
    return audio.contains(QFileInfo(path).suffix().toLower());
}

// Finds a file that moved: the stored path, the path relative to the project file, then a file of
// the same name in the project's folder tree (a few thousand entries at most).
class MediaFinder
{
public:
    explicit MediaFinder(const QString &projectFile)
        : m_dir(QFileInfo(projectFile).absolutePath())
    {
    }

    QString find(const QString &stored, const QString &relative, bool *found)
    {
        const QString key = stored + QLatin1Char('\x1f') + relative;
        const auto cached = m_cache.constFind(key);
        if (cached != m_cache.constEnd()) {
            *found = cached->second;
            return cached->first;
        }
        QString result = stored;
        bool ok = false;
        QString normalized = stored;
        normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (!normalized.isEmpty() && QFileInfo(normalized).isFile()) {
            result = QFileInfo(normalized).absoluteFilePath();
            ok = true;
        }
        if (!ok && !relative.isEmpty()) {
            QString rel = relative;
            rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
            const QFileInfo info(QDir(m_dir).filePath(rel));
            if (info.isFile()) {
                result = info.absoluteFilePath();
                ok = true;
            }
        }
        if (!ok) {
            const QString name = QFileInfo(normalized).fileName();
            if (!name.isEmpty()) {
                indexFolder();
                const QString hit = m_byName.value(name.toLower());
                if (!hit.isEmpty()) {
                    result = hit;
                    ok = true;
                }
            }
        }
        if (!ok)
            result = normalized;
        m_cache.insert(key, {result, ok});
        *found = ok;
        return result;
    }

private:
    void indexFolder()
    {
        if (m_indexed)
            return;
        m_indexed = true;
        if (m_dir.isEmpty())
            return;
        QDirIterator it(m_dir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        int seen = 0;
        while (it.hasNext() && seen < 20000) {
            const QString path = it.next();
            ++seen;
            const QString name = it.fileName().toLower();
            if (!m_byName.contains(name))
                m_byName.insert(name, path);
        }
    }

    QString m_dir;
    bool m_indexed = false;
    QHash<QString, QString> m_byName;
    QHash<QString, QPair<QString, bool>> m_cache;
};

} // namespace

ProjectBuildResult toProject(const ImportedTimeline &timeline, const QString &projectFile)
{
    ProjectBuildResult result;
    Project &project = result.project;
    project.setId(newId());
    project.setName(timeline.name.isEmpty() ? QFileInfo(projectFile).completeBaseName() : timeline.name);
    project.setResolution(qMax(16, timeline.width), qMax(16, timeline.height));
    project.setFps(qBound(1, int(std::lround(timeline.fps)), 240));
    if (timeline.sampleRate >= 8000)
        project.setSampleRate(timeline.sampleRate);
    project.tracks().clear();

    MediaFinder finder(projectFile);
    QHash<QString, QString> assetForPath;
    QSet<QString> missing;
    const auto assetFor = [&](const ImportedClip &c, bool audioTrack, QString *path) {
        bool found = false;
        *path = finder.find(c.path, c.relativePath, &found);
        if (!found)
            missing.insert(QDir::toNativeSeparators(*path));
        const auto it = assetForPath.constFind(*path);
        if (it != assetForPath.constEnd())
            return *it;
        MediaAsset asset;
        asset.id = newId();
        asset.path = *path;
        asset.name = QFileInfo(*path).fileName();
        // The file's own kind: the sound of a video file on an audio track is still a video asset.
        Q_UNUSED(audioTrack);
        asset.kind = c.still ? MediaKind::Image : isAudioSuffix(*path) ? MediaKind::Audio : MediaKind::Video;
        project.assets().insert(asset.id, asset);
        project.assetOrder().append(asset.id);
        assetForPath.insert(*path, asset.id);
        return asset.id;
    };

    const auto makeClip = [&](const ImportedClip &c, bool audioTrack) {
        Clip clip;
        clip.id = newId();
        QString path;
        clip.assetId = assetFor(c, audioTrack, &path);
        clip.path = path;
        clip.name = c.name.isEmpty() ? QFileInfo(path).fileName() : c.name;
        clip.type = audioTrack ? ClipType::Audio : (c.still ? ClipType::Image : ClipType::Video);
        clip.timelineStart = qMax<int64_t>(0, c.startUs);
        clip.timelineDuration = qMax<int64_t>(1000, c.durationUs);
        clip.speed = c.speed > 0 ? c.speed : 1.0;
        clip.reverse = c.reverse;
        if (c.still) {
            clip.srcIn = 0;
            clip.srcOut = clip.timelineDuration;
            clip.speed = 1.0;
        } else {
            clip.srcIn = qMax<int64_t>(0, c.srcInUs);
            clip.srcOut = clip.srcIn + clip.sourceSpanUs();
        }
        if (clip.type == ClipType::Video)
            clip.suppressEmbeddedAudio = !c.keepsOwnAudio;
        ++result.clipCount;
        return clip;
    };

    for (const ImportedTrack &in : timeline.tracks) {
        if (in.audio) {
            Track track;
            track.type = TrackType::Audio;
            track.name = in.name;
            track.muted = in.muted;
            track.locked = in.locked;
            for (const ImportedClip &c : in.clips)
                track.clips.append(makeClip(c, true));
            project.tracks().append(track);
            continue;
        }
        Track pictures;
        pictures.type = TrackType::Shape;
        Track video;
        video.type = TrackType::Video;
        video.name = in.name;
        for (Track *t : {&pictures, &video}) {
            t->hidden = in.muted;
            t->locked = in.locked;
        }
        for (const ImportedClip &c : in.clips)
            (c.still ? pictures : video).clips.append(makeClip(c, false));
        if (!pictures.clips.isEmpty())
            project.tracks().append(pictures);
        if (!video.clips.isEmpty() || pictures.clips.isEmpty())
            project.tracks().append(video);
    }

    // Link each video clip with the audio clip that came with it: the same file at the same
    // place, within a frame.
    const int64_t frame = int64_t(1'000'000 / qMax(1, project.fps()));
    QSet<QString> linked;
    for (Track &vt : project.tracks()) {
        if (vt.type != TrackType::Video)
            continue;
        for (Clip &v : vt.clips) {
            for (Track &at : project.tracks()) {
                if (at.type != TrackType::Audio || !v.linkId.isEmpty())
                    continue;
                for (Clip &a : at.clips) {
                    if (linked.contains(a.id) || a.path != v.path || qAbs(a.timelineStart - v.timelineStart) > frame
                        || qAbs(a.timelineEnd() - v.timelineEnd()) > frame)
                        continue;
                    const QString link = newId();
                    v.linkId = link;
                    a.linkId = link;
                    v.suppressEmbeddedAudio = true;
                    linked.insert(a.id);
                    break;
                }
            }
        }
    }

    if (project.tracks().isEmpty())
        project.resetToDefaultTimeline();
    project.ensureTrackIds();
    result.missingMedia = QStringList(missing.cbegin(), missing.cend());
    result.missingMedia.sort(Qt::CaseInsensitive);
    return result;
}

QString summary(const ImportedTimeline &timeline, const ProjectBuildResult &result, const QString &appName)
{
    QStringList parts;
    parts.append(tr("%1 project imported: %n clip(s).", nullptr, result.clipCount).arg(appName));
    if (timeline.skippedClips > 0)
        parts.append(tr("Left out %n item(s) Dafin can't bring over (%1).", nullptr, timeline.skippedClips)
                         .arg(timeline.skippedKinds.join(QStringLiteral(", "))));
    if (!result.missingMedia.isEmpty())
        parts.append(tr("%n media file(s) were not found and show as missing.", nullptr,
                        int(result.missingMedia.size())));
    parts.append(tr("Effects, transitions, titles and keyframes stay behind."));
    for (const QString &note : timeline.notes)
        parts.append(note);
    return parts.join(QLatin1Char(' '));
}

} // namespace drift::interchange
