#include "core/interchange/filmora/FilmoraProjectParser.h"

#include "core/base/ZipArchive.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include <algorithm>

namespace drift::interchange::filmora {

namespace {

constexpr double kTicksPerUs = 10.0; // 100 ns units

QString tr(const char *text, const char *disambiguation = nullptr, int n = -1)
{
    return QCoreApplication::translate("FilmoraImport", text, disambiguation, n);
}

int64_t ticksToUs(const QJsonValue &v)
{
    return int64_t(v.toDouble() / kTicksPerUs);
}

bool isStillSuffix(const QString &path)
{
    static const QStringList stills{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                    QStringLiteral("bmp"), QStringLiteral("gif"), QStringLiteral("webp"),
                                    QStringLiteral("tif"), QStringLiteral("tiff"), QStringLiteral("heic"),
                                    QStringLiteral("svg")};
    return stills.contains(QFileInfo(path).suffix().toLower());
}

QJsonObject readJson(QFile &file, const QList<drift::zip::Entry> &entries, const QString &path)
{
    for (const drift::zip::Entry &e : entries) {
        if (e.isDir || e.path.compare(path, Qt::CaseInsensitive) != 0)
            continue;
        QByteArray data;
        if (!drift::zip::extractEntry(file, e, data))
            return {};
        // Some writers put a byte-order mark first.
        if (data.startsWith("\xEF\xBB\xBF"))
            data.remove(0, 3);
        return QJsonDocument::fromJson(data).object();
    }
    return {};
}

// A speed curve with every key at the same value is a constant speed; Filmora writes one for
// every clip. Returns 0 when the curve really ramps.
double constantSpeedOf(const QJsonValue &speedParam)
{
    QJsonObject curve;
    if (speedParam.isObject())
        curve = speedParam.toObject();
    else if (speedParam.isString())
        curve = QJsonDocument::fromJson(speedParam.toString().toUtf8()).object();
    const QJsonArray keys = curve.value(QStringLiteral("keyframeSets")).toArray();
    double first = -1;
    for (const QJsonValue &k : keys) {
        const double v = k.toObject().value(QStringLiteral("_value")).toDouble(-1);
        if (v <= 0)
            continue;
        if (first < 0)
            first = v;
        else if (qAbs(v - first) > 1e-4)
            return 0;
    }
    return first > 0 ? first : 1.0;
}

} // namespace

QString localPathFromFilmora(const QString &uriOrPath)
{
    QString s = uriOrPath.trimmed();
    if (s.startsWith(QLatin1String("file:"), Qt::CaseInsensitive)) {
        const QUrl url(s);
        QString local = url.isLocalFile() ? url.toLocalFile() : QString();
        if (local.isEmpty()) {
            // "file:/C:/x" — one slash, which QUrl reads as a path on no host.
            local = QUrl::fromPercentEncoding(s.mid(5).toUtf8());
            while (local.startsWith(QLatin1String("//")))
                local.remove(0, 1);
        }
        // "/C:/Users/…" on Windows.
        if (local.size() > 3 && local.at(0) == QLatin1Char('/') && local.at(2) == QLatin1Char(':'))
            local.remove(0, 1);
        s = local;
    }
    return s;
}

std::optional<ImportedTimeline> parse(const QString &wfpPath, QString *error)
{
    QFile file(wfpPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = tr("Can't open %1.").arg(QFileInfo(wfpPath).fileName());
        return std::nullopt;
    }
    QString zipError;
    const QList<drift::zip::Entry> entries = drift::zip::readEntries(file, &zipError);
    if (entries.isEmpty()) {
        if (error)
            *error = tr("%1 is not a Filmora project Dafin can read.").arg(QFileInfo(wfpPath).fileName());
        return std::nullopt;
    }

    const QJsonObject info = readJson(file, entries, QStringLiteral("ProjectFolder/project_info.json"));
    QString timelinePath;
    const QString mediaId = info.value(QStringLiteral("timeline_mediaId")).toString();
    if (!mediaId.isEmpty())
        timelinePath = QStringLiteral("ProjectFolder/Medias/%1/timeline.wesproj").arg(mediaId);
    QJsonObject root = timelinePath.isEmpty() ? QJsonObject() : readJson(file, entries, timelinePath);
    if (root.isEmpty()) {
        // Any timeline the archive carries.
        for (const drift::zip::Entry &e : entries) {
            if (!e.isDir && e.path.endsWith(QLatin1String(".wesproj"), Qt::CaseInsensitive)) {
                root = readJson(file, entries, e.path);
                if (!root.value(QStringLiteral("timelineInfos")).toArray().isEmpty())
                    break;
            }
        }
    }
    const QJsonArray timelines = root.value(QStringLiteral("timelineInfos")).toArray();
    if (timelines.isEmpty()) {
        if (error)
            *error = tr("This Filmora project has no timeline Dafin can read. Projects saved by Filmora 15 and "
                        "later work; open an older one in a current Filmora and save it again.");
        return std::nullopt;
    }
    // The main timeline is the one project_info names, else the first.
    QJsonObject timelineObj = timelines.first().toObject();
    const int currentId = root.value(QStringLiteral("currentTimelineId")).toInt(-1);
    for (const QJsonValue &v : timelines) {
        if (v.toObject().value(QStringLiteral("timelineId")).toInt(-2) == currentId)
            timelineObj = v.toObject();
    }

    ImportedTimeline timeline;
    timeline.name = info.value(QStringLiteral("project_file_name")).toString(QFileInfo(wfpPath).completeBaseName());
    const QJsonObject rate = timelineObj.value(QStringLiteral("frameRate")).toObject();
    const double num = rate.value(QStringLiteral("num")).toDouble();
    const double den = rate.value(QStringLiteral("den")).toDouble(1);
    if (num > 0 && den > 0)
        timeline.fps = num / den;
    else {
        const QJsonArray r = info.value(QStringLiteral("project_timeline_framerate")).toArray();
        if (r.size() == 2 && r.at(0).toDouble() > 0 && r.at(1).toDouble() > 0)
            timeline.fps = r.at(0).toDouble() / r.at(1).toDouble();
    }
    const int w = timelineObj.value(QStringLiteral("resolutionWidth")).toInt();
    const int h = timelineObj.value(QStringLiteral("resolutionHeight")).toInt();
    if (w > 0 && h > 0) {
        timeline.width = w;
        timeline.height = h;
    } else {
        const QJsonArray r = info.value(QStringLiteral("project_timeline_resolution")).toArray();
        if (r.size() == 2 && r.at(0).toInt() > 0 && r.at(1).toInt() > 0) {
            timeline.width = r.at(0).toInt();
            timeline.height = r.at(1).toInt();
        }
    }
    timeline.sampleRate = timelineObj.value(QStringLiteral("sampleRate"))
                              .toInt(info.value(QStringLiteral("project_sample_rate")).toInt());

    QHash<QString, int> skipped;
    struct Numbered
    {
        int number;
        int order;
        ImportedTrack track;
    };
    QList<Numbered> video;
    QList<Numbered> audio;
    const QJsonArray trackInfos = timelineObj.value(QStringLiteral("trackInfos")).toArray();
    for (int t = 0; t < trackInfos.size(); ++t) {
        const QJsonObject track = trackInfos.at(t).toObject();
        const int type = track.value(QStringLiteral("trackType")).toInt();
        const bool isAudio = type == 2;
        const bool isVideo = type == 1;
        ImportedTrack out;
        out.audio = isAudio;
        out.muted = track.value(QStringLiteral("mute")).toBool() || track.value(QStringLiteral("hide")).toBool()
                    || track.value(QStringLiteral("isMute")).toBool() || track.value(QStringLiteral("isHide")).toBool();
        out.locked = track.value(QStringLiteral("lock")).toBool() || track.value(QStringLiteral("isLock")).toBool();
        for (const QJsonValue &cv : track.value(QStringLiteral("clipList")).toArray()) {
            const QJsonObject clip = cv.toObject();
            const QString filename = clip.value(QStringLiteral("filename")).toString();
            if (filename.isEmpty() || (!isAudio && !isVideo)) {
                skipped[tr("titles, effects and stickers")] += 1;
                continue;
            }
            const int64_t begin = ticksToUs(clip.value(QStringLiteral("tlBegin")));
            const int64_t end = ticksToUs(clip.value(QStringLiteral("tlEnd")));
            if (end <= begin)
                continue;
            ImportedClip c;
            c.path = localPathFromFilmora(filename);
            c.name = QFileInfo(c.path).fileName();
            c.startUs = begin;
            c.durationUs = end - begin;
            c.srcInUs = ticksToUs(clip.value(QStringLiteral("inPoint")));
            const int64_t srcOut = ticksToUs(clip.value(QStringLiteral("outPoint")));
            const QJsonObject speed = clip.value(QStringLiteral("speed")).toObject();
            c.reverse = speed.value(QStringLiteral("reverse")).toBool();
            c.still = isVideo && isStillSuffix(c.path);
            // The source span over the timeline span is the speed; the curve says the same for a
            // constant speed and is only read to spot a ramp.
            double rateValue = srcOut > c.srcInUs ? double(srcOut - c.srcInUs) / double(c.durationUs) : 0.0;
            const double curve = constantSpeedOf(speed.value(QStringLiteral("speedParam")));
            if (curve <= 0) {
                if (!timeline.notes.contains(tr("Speed ramps became constant speeds.")))
                    timeline.notes.append(tr("Speed ramps became constant speeds."));
            } else if (rateValue <= 0) {
                rateValue = curve;
            }
            c.speed = c.still ? 1.0 : (rateValue > 0.01 && rateValue < 100.0 ? rateValue : 1.0);
            if (qAbs(c.speed - 1.0) < 1e-3)
                c.speed = 1.0;
            if (c.still) {
                c.srcInUs = 0;
                c.reverse = false;
            }
            out.clips.append(c);
        }
        if (!isAudio && !isVideo)
            continue; // title, effect and sticker tracks: their clips were counted above
        std::stable_sort(out.clips.begin(), out.clips.end(),
                         [](const ImportedClip &a, const ImportedClip &b) { return a.startUs < b.startUs; });
        const int number = track.value(QStringLiteral("trackTag")).toInt(t + 1);
        (isAudio ? audio : video).append({number, t, out});
    }

    // Filmora numbers tracks from the bottom: track 1 is the lowest video track.
    std::stable_sort(video.begin(), video.end(), [](const Numbered &a, const Numbered &b) {
        return a.number != b.number ? a.number > b.number : a.order > b.order;
    });
    std::stable_sort(audio.begin(), audio.end(), [](const Numbered &a, const Numbered &b) {
        return a.number != b.number ? a.number < b.number : a.order < b.order;
    });

    // A video clip whose sound Filmora keeps as a follower clip on an audio track has that sound
    // there; one without such a clip plays the sound of its own file.
    for (Numbered &v : video) {
        for (ImportedClip &c : v.track.clips) {
            bool follower = false;
            for (const Numbered &a : std::as_const(audio)) {
                for (const ImportedClip &ac : a.track.clips) {
                    if (ac.path == c.path && qAbs(ac.startUs - c.startUs) < 50000
                        && qAbs(ac.durationUs - c.durationUs) < 50000) {
                        follower = true;
                        break;
                    }
                }
                if (follower)
                    break;
            }
            c.keepsOwnAudio = !follower && !c.still;
        }
    }
    for (const Numbered &v : std::as_const(video))
        timeline.tracks.append(v.track);
    for (const Numbered &a : std::as_const(audio))
        timeline.tracks.append(a.track);
    for (auto it = skipped.constBegin(); it != skipped.constEnd(); ++it) {
        timeline.skippedClips += it.value();
        timeline.skippedKinds.append(it.key());
    }
    return timeline;
}

} // namespace drift::interchange::filmora
