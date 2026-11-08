#include "core/interchange/premiere/PremiereProjectParser.h"

#include <QCoreApplication>
#include <QDomDocument>
#include <QHash>
#include <QSet>
#include <QMap>
#include <QRegularExpression>
#include <QtMath>

#include <algorithm>
#include <cstring>
#include <zlib.h>

namespace drift::interchange::premiere {

namespace {

constexpr qint64 kTicksPerSecond = 254016000000LL;
constexpr qint64 kTicksPerUs = 254016LL;
constexpr int kMaxNesting = 6;

QString tr(const char *text, const char *disambiguation = nullptr, int n = -1)
{
    return QCoreApplication::translate("PremiereImport", text, disambiguation, n);
}

int64_t ticksToUs(qint64 ticks)
{
    return ticks / kTicksPerUs;
}

qint64 ticksOf(const QDomElement &parent, const QString &tag, qint64 fallback = 0)
{
    const QDomElement e = parent.firstChildElement(tag);
    if (e.isNull())
        return fallback;
    bool ok = false;
    const qint64 v = e.text().trimmed().toLongLong(&ok);
    return ok ? v : fallback;
}

bool flagOf(const QDomElement &parent, const QString &tag)
{
    return parent.firstChildElement(tag).text().trimmed() == QLatin1String("true");
}

// The top-level object table of <PremiereData>, by ObjectID and ObjectUID.
struct Objects
{
    QHash<QString, QDomElement> byId;
    QHash<QString, QDomElement> byUid;

    QDomElement resolve(const QDomElement &ref) const
    {
        if (ref.isNull())
            return {};
        const QString id = ref.attribute(QStringLiteral("ObjectRef"));
        if (!id.isEmpty())
            return byId.value(id);
        const QString uid = ref.attribute(QStringLiteral("ObjectURef"));
        if (!uid.isEmpty())
            return byUid.value(uid);
        return {};
    }
    QDomElement follow(const QDomElement &parent, const QString &tag) const
    {
        return resolve(parent.firstChildElement(tag));
    }
};

struct RawClip
{
    ImportedClip clip;
    // Set for a clip that plays a nested sequence instead of a file.
    QString nestedSequenceUid;
    int64_t nestedInUs = 0;
};

struct RawTrack
{
    bool audio = false;
    QString name;
    bool muted = false;
    bool locked = false;
    QList<RawClip> clips;
};

struct RawSequence
{
    QString uid;
    QString name;
    int width = 0;
    int height = 0;
    qint64 frameTicks = 0;
    int sampleRate = 0;
    QList<RawTrack> video;  // V1 first
    QList<RawTrack> audio;  // A1 first
};

class Reader
{
public:
    explicit Reader(const Objects &objects) : m_objects(objects) {}

    RawSequence readSequence(const QDomElement &sequence);
    QHash<QString, int> skipped;  // kind → count

private:
    RawTrack readTrack(const QDomElement &track, bool audio);
    bool readItem(const QDomElement &item, bool audio, RawClip *out);

    const Objects &m_objects;
};

void noteSkip(QHash<QString, int> &skipped, const QString &kind)
{
    skipped[kind] += 1;
}

RawSequence Reader::readSequence(const QDomElement &sequence)
{
    RawSequence seq;
    seq.uid = sequence.attribute(QStringLiteral("ObjectUID"));
    seq.name = sequence.firstChildElement(QStringLiteral("Name")).text().trimmed();
    const QDomElement groups = sequence.firstChildElement(QStringLiteral("TrackGroups"));
    for (QDomElement pair = groups.firstChildElement(QStringLiteral("TrackGroup")); !pair.isNull();
         pair = pair.nextSiblingElement(QStringLiteral("TrackGroup"))) {
        const QDomElement group = m_objects.follow(pair, QStringLiteral("Second"));
        if (group.isNull())
            continue;
        const bool video = group.tagName() == QLatin1String("VideoTrackGroup");
        const bool audio = group.tagName() == QLatin1String("AudioTrackGroup");
        if (!video && !audio)
            continue; // captions (DataTrackGroup)
        const QDomElement inner = group.firstChildElement(QStringLiteral("TrackGroup"));
        if (video) {
            seq.frameTicks = ticksOf(inner, QStringLiteral("FrameRate"));
            const QStringList rect = group.firstChildElement(QStringLiteral("FrameRect")).text().split(QLatin1Char(','));
            if (rect.size() == 4) {
                seq.width = rect.at(2).trimmed().toInt();
                seq.height = rect.at(3).trimmed().toInt();
            }
        } else {
            const qint64 audioTicks = ticksOf(inner, QStringLiteral("FrameRate"));
            if (audioTicks > 0)
                seq.sampleRate = int(qRound64(double(kTicksPerSecond) / double(audioTicks)));
        }
        // Tracks in their Index order: V1 / A1 first.
        QList<QPair<int, QDomElement>> ordered;
        const QDomElement tracks = inner.firstChildElement(QStringLiteral("Tracks"));
        for (QDomElement ref = tracks.firstChildElement(QStringLiteral("Track")); !ref.isNull();
             ref = ref.nextSiblingElement(QStringLiteral("Track"))) {
            const QDomElement track = m_objects.resolve(ref);
            if (!track.isNull())
                ordered.append({ref.attribute(QStringLiteral("Index")).toInt(), track});
        }
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](const auto &a, const auto &b) { return a.first < b.first; });
        for (const auto &entry : std::as_const(ordered))
            (video ? seq.video : seq.audio).append(readTrack(entry.second, audio));
    }
    return seq;
}

RawTrack Reader::readTrack(const QDomElement &track, bool audio)
{
    RawTrack out;
    out.audio = audio;
    const QDomElement clipTrack = track.firstChildElement(QStringLiteral("ClipTrack"));
    const QDomElement core = clipTrack.firstChildElement(QStringLiteral("Track"));
    if (!audio) {
        out.muted = flagOf(core, QStringLiteral("IsMuted"));
    } else {
        // An audio track's mute is the Mute parameter of its fader in the mix graph:
        // AudioTrack/ComponentOwner/Components → chain → components → AudioComponent params.
        const QDomElement chain = m_objects.follow(
            track.firstChildElement(QStringLiteral("AudioTrack")).firstChildElement(QStringLiteral("ComponentOwner")),
            QStringLiteral("Components"));
        const QDomElement components = chain.firstChildElement(QStringLiteral("ComponentChain"))
                                           .firstChildElement(QStringLiteral("Components"));
        for (QDomElement ref = components.firstChildElement(QStringLiteral("Component")); !ref.isNull() && !out.muted;
             ref = ref.nextSiblingElement(QStringLiteral("Component"))) {
            const QDomElement params = m_objects.resolve(ref)
                                           .firstChildElement(QStringLiteral("AudioComponent"))
                                           .firstChildElement(QStringLiteral("Component"))
                                           .firstChildElement(QStringLiteral("Params"));
            for (QDomElement pref = params.firstChildElement(QStringLiteral("Param")); !pref.isNull();
                 pref = pref.nextSiblingElement(QStringLiteral("Param"))) {
                const QDomElement param = m_objects.resolve(pref);
                if (param.firstChildElement(QStringLiteral("Name")).text() != QLatin1String("Mute"))
                    continue;
                const QString key = param.firstChildElement(QStringLiteral("StartKeyframe")).text();
                out.muted = !key.isEmpty() ? key.section(QLatin1Char(','), 1, 1) == QLatin1String("true")
                                           : param.firstChildElement(QStringLiteral("CurrentValue")).text() == QLatin1String("true");
                break;
            }
        }
    }
    out.locked = flagOf(core, QStringLiteral("IsLocked"));
    out.name = core.firstChildElement(QStringLiteral("Name")).text().trimmed();
    const QDomElement items = clipTrack.firstChildElement(QStringLiteral("ClipItems")).firstChildElement(QStringLiteral("TrackItems"));
    for (QDomElement ref = items.firstChildElement(QStringLiteral("TrackItem")); !ref.isNull();
         ref = ref.nextSiblingElement(QStringLiteral("TrackItem"))) {
        RawClip clip;
        if (readItem(m_objects.resolve(ref), audio, &clip))
            out.clips.append(clip);
    }
    std::stable_sort(out.clips.begin(), out.clips.end(),
                     [](const RawClip &a, const RawClip &b) { return a.clip.startUs < b.clip.startUs; });
    return out;
}

bool Reader::readItem(const QDomElement &item, bool audio, RawClip *out)
{
    if (item.isNull())
        return false;
    const QDomElement clipTrackItem = item.firstChildElement(QStringLiteral("ClipTrackItem"));
    if (clipTrackItem.isNull())
        return false;
    if (flagOf(clipTrackItem, QStringLiteral("IsMuted"))) {
        noteSkip(skipped, tr("disabled clips"));
        return false;
    }
    const QDomElement timing = clipTrackItem.firstChildElement(QStringLiteral("TrackItem"));
    const qint64 start = ticksOf(timing, QStringLiteral("Start"));
    const qint64 end = ticksOf(timing, QStringLiteral("End"));
    if (end <= start)
        return false;

    const QDomElement subClip = m_objects.follow(clipTrackItem, QStringLiteral("SubClip"));
    const QDomElement holder = m_objects.follow(subClip, QStringLiteral("Clip"));
    const QDomElement clip = holder.firstChildElement(QStringLiteral("Clip"));
    if (clip.isNull()) {
        noteSkip(skipped, audio ? tr("audio clips") : tr("video clips"));
        return false;
    }

    ImportedClip &c = out->clip;
    c.name = subClip.firstChildElement(QStringLiteral("Name")).text().trimmed();
    c.startUs = ticksToUs(start);
    c.durationUs = ticksToUs(end) - c.startUs;
    const QString speedText = clip.firstChildElement(QStringLiteral("PlaybackSpeed")).text().trimmed();
    bool speedOk = false;
    const double speed = speedText.toDouble(&speedOk);
    c.speed = speedOk && qAbs(speed) > 1e-4 ? qAbs(speed) : 1.0;
    c.reverse = flagOf(clip, QStringLiteral("PlayBackwards")) || (speedOk && speed < 0);
    // In and out are where the clip starts and ends in its source; the timeline length is that
    // span divided by the speed.
    c.srcInUs = ticksToUs(ticksOf(clip, QStringLiteral("InPoint")));

    const QDomElement source = m_objects.follow(clip, QStringLiteral("Source"));
    const QString sourceTag = source.tagName();
    if (sourceTag == QLatin1String("VideoSequenceSource") || sourceTag == QLatin1String("AudioSequenceSource")) {
        const QDomElement nested = m_objects.follow(source.firstChildElement(QStringLiteral("SequenceSource")),
                                                    QStringLiteral("Sequence"));
        if (nested.isNull()) {
            noteSkip(skipped, tr("nested sequences"));
            return false;
        }
        out->nestedSequenceUid = nested.attribute(QStringLiteral("ObjectUID"));
        out->nestedInUs = c.srcInUs;
        return true;
    }
    const QDomElement mediaSource = source.firstChildElement(QStringLiteral("MediaSource"));
    const QDomElement media = m_objects.follow(mediaSource, QStringLiteral("Media"));
    if (media.isNull()) {
        // Graphics and titles (Essential Graphics, legacy titles, .mogrt) render without a file.
        noteSkip(skipped, tr("titles and graphics"));
        return false;
    }
    QString path = media.firstChildElement(QStringLiteral("ActualMediaFilePath")).text().trimmed();
    if (path.isEmpty())
        path = media.firstChildElement(QStringLiteral("FilePath")).text().trimmed();
    // Generated media (black video, colour matte, bars and tone, adjustment layers) stores a
    // four-character code where the path goes.
    static const QRegularExpression digits(QStringLiteral("^\\d+$"));
    if (path.isEmpty() || digits.match(path).hasMatch()) {
        noteSkip(skipped, tr("generated media (colour mattes, adjustment layers…)"));
        return false;
    }
    c.path = path;
    c.relativePath = media.firstChildElement(QStringLiteral("RelativePath")).text().trimmed();
    c.still = flagOf(media, QStringLiteral("Infinite")) && !audio;
    if (c.name.isEmpty())
        c.name = media.firstChildElement(QStringLiteral("Title")).text().trimmed();
    if (c.still) {
        c.srcInUs = 0;
        c.speed = 1.0;
        c.reverse = false;
    }
    return true;
}

// Nested sequences are laid out in the parent, clip by clip: each of the nested sequence's tracks
// goes on a lane of the parent track (lane 0 is the track itself, further lanes become tracks of
// their own next to it), cut to the part of the nested sequence the parent clip shows.
struct Lane
{
    QList<ImportedClip> clips;
};

// `tracks` are a nested sequence's; [windowInUs, windowOutUs) is the part of it the parent clip
// shows, `offsetUs` is where on the parent timeline windowInUs falls, and `scale` is how many
// parent microseconds one nested microsecond takes (1 / the parent clip's speed).
void placeFlattened(const QHash<QString, RawSequence> &sequences, const QList<RawTrack> &tracks, bool audio,
                    int64_t offsetUs, int64_t windowInUs, int64_t windowOutUs, double scale, int depth,
                    QList<Lane> &lanes, int laneBase, QHash<QString, int> &skipped, int *lanesUsed)
{
    for (int t = 0; t < tracks.size(); ++t) {
        const int lane = laneBase + t;
        while (lanes.size() <= lane)
            lanes.append(Lane{});
        *lanesUsed = qMax(*lanesUsed, lane + 1);
        for (const RawClip &raw : tracks.at(t).clips) {
            const int64_t start = raw.clip.startUs;
            const int64_t end = start + raw.clip.durationUs;
            const int64_t from = qMax(start, windowInUs);
            const int64_t to = qMin(end, windowOutUs);
            if (to <= from)
                continue;
            const int64_t placedAt = offsetUs + int64_t(double(from - windowInUs) * scale);
            if (!raw.nestedSequenceUid.isEmpty()) {
                const auto it = sequences.constFind(raw.nestedSequenceUid);
                if (it == sequences.constEnd() || depth >= kMaxNesting) {
                    skipped[tr("nested sequences")] += 1;
                    continue;
                }
                const int64_t innerIn = raw.nestedInUs + int64_t(double(from - start) * raw.clip.speed);
                const int64_t innerOut = raw.nestedInUs + int64_t(double(to - start) * raw.clip.speed);
                placeFlattened(sequences, audio ? it->audio : it->video, audio, placedAt, innerIn, innerOut,
                               scale / raw.clip.speed, depth + 1, lanes, lane, skipped, lanesUsed);
                continue;
            }
            ImportedClip c = raw.clip;
            // Trimmed to the window, the source kept in step at the clip's speed. A reversed clip
            // plays its source backwards, so its head is the source's end.
            c.startUs = placedAt;
            c.durationUs = int64_t(double(to - from) * scale);
            if (!c.still) {
                c.srcInUs += int64_t(double(c.reverse ? end - to : from - start) * c.speed);
                // A nested sequence played slower or faster plays its clips that much slower or faster.
                c.speed = c.speed / scale;
            }
            lanes[lane].clips.append(c);
        }
    }
}

} // namespace

bool looksLikePremiereXml(const QByteArray &head)
{
    return head.contains("<PremiereData");
}

QByteArray gunzip(const QByteArray &data)
{
    if (data.size() < 2 || uchar(data[0]) != 0x1f || uchar(data[1]) != 0x8b)
        return data;
    z_stream strm;
    std::memset(&strm, 0, sizeof(strm));
    if (inflateInit2(&strm, 32 + MAX_WBITS) != Z_OK)
        return {};
    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    strm.avail_in = uInt(data.size());
    QByteArray out;
    QByteArray buffer(1 << 16, Qt::Uninitialized);
    int ret = Z_OK;
    while (ret == Z_OK) {
        strm.next_out = reinterpret_cast<Bytef *>(buffer.data());
        strm.avail_out = uInt(buffer.size());
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&strm);
            return {};
        }
        out.append(buffer.constData(), buffer.size() - int(strm.avail_out));
    }
    inflateEnd(&strm);
    return out;
}

std::optional<ImportedTimeline> parse(const QByteArray &data, QString *error, const QString &sequenceName)
{
    const QByteArray xml = gunzip(data);
    if (xml.isEmpty()) {
        if (error)
            *error = tr("The Premiere Pro project could not be decompressed.");
        return std::nullopt;
    }
    QDomDocument doc;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const QDomDocument::ParseResult parsed = doc.setContent(xml);
    if (!parsed) {
        if (error)
            *error = tr("The Premiere Pro project is damaged (XML error on line %1: %2).")
                         .arg(parsed.errorLine)
                         .arg(parsed.errorMessage);
        return std::nullopt;
    }
#else
    QString message;
    int line = 0;
    if (!doc.setContent(xml, &message, &line)) {
        if (error)
            *error = tr("The Premiere Pro project is damaged (XML error on line %1: %2).").arg(line).arg(message);
        return std::nullopt;
    }
#endif
    const QDomElement root = doc.documentElement();
    if (root.tagName() != QLatin1String("PremiereData")) {
        if (error)
            *error = tr("This is not a Premiere Pro project.");
        return std::nullopt;
    }

    Objects objects;
    QList<QDomElement> sequenceElements;
    QString projectName;
    QMap<int, QString> openByNumber;
    for (QDomElement e = root.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
        const QString id = e.attribute(QStringLiteral("ObjectID"));
        if (!id.isEmpty())
            objects.byId.insert(id, e);
        const QString uid = e.attribute(QStringLiteral("ObjectUID"));
        if (!uid.isEmpty())
            objects.byUid.insert(uid, e);
        if (e.tagName() == QLatin1String("Sequence"))
            sequenceElements.append(e);
        if (e.tagName() == QLatin1String("Project") && projectName.isEmpty())
            projectName = e.firstChildElement(QStringLiteral("Name")).text().trimmed();
    }
    // The sequences open in Premiere's timeline panel, in the order they were opened; the last
    // one is the one in front.
    const QDomNodeList props = doc.elementsByTagName(QStringLiteral("Properties"));
    for (int i = 0; i < props.size(); ++i) {
        for (QDomElement p = props.at(i).firstChildElement(); !p.isNull(); p = p.nextSiblingElement()) {
            static const QString prefix = QStringLiteral("MZ.PrefixKey.OpenSequenceGuidList.");
            if (p.tagName().startsWith(prefix)) {
                bool ok = false;
                const int n = p.tagName().mid(prefix.size()).toInt(&ok);
                if (ok && !p.text().trimmed().isEmpty())
                    openByNumber.insert(n, p.text().trimmed());
            }
        }
        if (!openByNumber.isEmpty())
            break;
    }

    if (sequenceElements.isEmpty()) {
        if (error)
            *error = tr("The Premiere Pro project has no sequence to import.");
        return std::nullopt;
    }

    Reader reader(objects);
    QHash<QString, RawSequence> sequences;
    QStringList order;
    for (const QDomElement &e : std::as_const(sequenceElements)) {
        RawSequence seq = reader.readSequence(e);
        if (seq.uid.isEmpty())
            seq.uid = QStringLiteral("seq-%1").arg(order.size());
        order.append(seq.uid);
        sequences.insert(seq.uid, seq);
    }

    QString chosen;
    for (const QString &uid : std::as_const(order)) {
        if (!sequenceName.isEmpty() && sequences.value(uid).name == sequenceName)
            chosen = uid;
    }
    const QList<int> openNumbers = openByNumber.keys();
    for (auto n = openNumbers.crbegin(); n != openNumbers.crend() && chosen.isEmpty(); ++n) {
        if (sequences.contains(openByNumber.value(*n)))
            chosen = openByNumber.value(*n);
    }
    if (chosen.isEmpty()) {
        // The sequence with the most clips, the first of those.
        int best = -1;
        for (const QString &uid : std::as_const(order)) {
            int count = 0;
            for (const RawTrack &t : sequences.value(uid).video)
                count += int(t.clips.size());
            for (const RawTrack &t : sequences.value(uid).audio)
                count += int(t.clips.size());
            if (count > best) {
                best = count;
                chosen = uid;
            }
        }
    }
    const RawSequence &main = sequences[chosen];

    ImportedTimeline timeline;
    timeline.name = !main.name.isEmpty() ? main.name : projectName;
    if (main.width > 0 && main.height > 0) {
        timeline.width = main.width;
        timeline.height = main.height;
    }
    if (main.frameTicks > 0)
        timeline.fps = double(kTicksPerSecond) / double(main.frameTicks);
    timeline.sampleRate = main.sampleRate;
    if (sequences.size() > 1)
        timeline.notes.append(tr("Imported the sequence “%1”; the project has %n sequence(s).", nullptr,
                                 int(sequences.size()))
                                  .arg(timeline.name));

    QHash<QString, int> skipped = reader.skipped;
    // Each top-level track with the lanes its nested sequences need.
    const auto flatten = [&](const QList<RawTrack> &tracks, bool audio) {
        QList<QList<Lane>> perTrack;
        for (const RawTrack &track : tracks) {
            QList<Lane> lanes;
            lanes.append(Lane{});
            int used = 1;
            for (const RawClip &raw : track.clips) {
                if (raw.nestedSequenceUid.isEmpty()) {
                    lanes[0].clips.append(raw.clip);
                    continue;
                }
                const auto it = sequences.constFind(raw.nestedSequenceUid);
                if (it == sequences.constEnd() || raw.nestedSequenceUid == main.uid) {
                    skipped[tr("nested sequences")] += 1;
                    continue;
                }
                const int64_t windowIn = raw.nestedInUs;
                const int64_t windowOut = raw.nestedInUs + int64_t(double(raw.clip.durationUs) * raw.clip.speed);
                placeFlattened(sequences, audio ? it->audio : it->video, audio, raw.clip.startUs, windowIn,
                               windowOut, 1.0 / raw.clip.speed, 1, lanes, 0, skipped, &used);
            }
            perTrack.append(lanes);
        }
        return perTrack;
    };

    const QList<QList<Lane>> videoLanes = flatten(main.video, false);
    const QList<QList<Lane>> audioLanes = flatten(main.audio, true);

    // Video: the highest track on top, and within a track its nested lanes above it.
    for (int t = int(videoLanes.size()) - 1; t >= 0; --t) {
        const QList<Lane> &lanes = videoLanes.at(t);
        for (int l = int(lanes.size()) - 1; l >= 0; --l) {
            if (l > 0 && lanes.at(l).clips.isEmpty())
                continue;
            ImportedTrack track;
            track.audio = false;
            track.name = l == 0 ? main.video.at(t).name : QString();
            track.muted = main.video.at(t).muted;
            track.locked = main.video.at(t).locked;
            track.clips = lanes.at(l).clips;
            std::stable_sort(track.clips.begin(), track.clips.end(),
                             [](const ImportedClip &a, const ImportedClip &b) { return a.startUs < b.startUs; });
            timeline.tracks.append(track);
        }
    }
    for (int t = 0; t < audioLanes.size(); ++t) {
        const QList<Lane> &lanes = audioLanes.at(t);
        for (int l = 0; l < lanes.size(); ++l) {
            if (l > 0 && lanes.at(l).clips.isEmpty())
                continue;
            ImportedTrack track;
            track.audio = true;
            track.name = l == 0 ? main.audio.at(t).name : QString();
            track.muted = main.audio.at(t).muted;
            track.locked = main.audio.at(t).locked;
            track.clips = lanes.at(l).clips;
            std::stable_sort(track.clips.begin(), track.clips.end(),
                             [](const ImportedClip &a, const ImportedClip &b) { return a.startUs < b.startUs; });
            timeline.tracks.append(track);
        }
    }
    for (auto it = skipped.constBegin(); it != skipped.constEnd(); ++it) {
        timeline.skippedClips += it.value();
        timeline.skippedKinds.append(it.key());
    }
    return timeline;
}

} // namespace drift::interchange::premiere
