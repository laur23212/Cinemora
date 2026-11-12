#include "engine/encode/export/SegmentedExport.h"

#include "core/project/Project.h"
#include "engine/project/ProjectDependencies.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QtGlobal>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
}

namespace SegmentedExport {

namespace {

constexpr int kJobVersion = 1;
const QLatin1String kJobFile("job.json");
const QLatin1String kProjectFile("project.json");

QString segmentName(int index, const QString &suffix)
{
    return QStringLiteral("seg-%1.%2").arg(index, 5, 10, QLatin1Char('0')).arg(suffix);
}

QString segmentPartName(int index, const QString &suffix)
{
    return QStringLiteral("seg-%1.part.%2").arg(index, 5, 10, QLatin1Char('0')).arg(suffix);
}

// The audio is stream-copied into the final file, so it is kept in a container of the same
// family: QuickTime carries the AAC encoder delay as an edit list the mp4/mov muxer understands,
// Matroska carries it as CodecDelay for mkv/webm.
QString audioSuffixFor(const QString &outputSuffix)
{
    const QString s = outputSuffix.toLower();
    if (s == QLatin1String("mp4") || s == QLatin1String("mov") || s == QLatin1String("m4v"))
        return QStringLiteral("mov");
    return QStringLiteral("mka");
}

QJsonObject readJob(const QString &dir)
{
    QFile file(QDir(dir).filePath(kJobFile));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

// QSaveFile: a crash while the job file is written leaves the previous one, never half a file.
bool writeJob(const QString &dir, const QJsonObject &job)
{
    QSaveFile file(QDir(dir).filePath(kJobFile));
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(job).toJson(QJsonDocument::Indented));
    return file.commit();
}

QJsonArray segmentsToJson(const QList<Segment> &segments)
{
    QJsonArray array;
    for (const Segment &s : segments)
        array.append(QJsonArray{double(s.firstFrame), double(s.frameCount)});
    return array;
}

QList<Segment> segmentsFromJson(const QJsonArray &array)
{
    QList<Segment> segments;
    for (const QJsonValue &v : array) {
        const QJsonArray pair = v.toArray();
        if (pair.size() != 2)
            return {};
        segments.append(Segment{static_cast<int64_t>(pair.at(0).toDouble()),
                                static_cast<int64_t>(pair.at(1).toDouble())});
    }
    return segments;
}

QSet<int> doneFromJson(const QJsonArray &array)
{
    QSet<int> done;
    for (const QJsonValue &v : array)
        done.insert(v.toInt());
    return done;
}

QJsonArray doneToJson(const QSet<int> &done)
{
    QList<int> sorted(done.begin(), done.end());
    std::sort(sorted.begin(), sorted.end());
    QJsonArray array;
    for (int i : sorted)
        array.append(i);
    return array;
}

bool isCancelled(const QString &error)
{
    return error == QLatin1String("Export cancelled") || error == QLatin1String("Cancelled");
}

// One demuxed input and the stream of `type` in it.
struct Input
{
    AVFormatContext *ctx = nullptr;
    int stream = -1;

    Input() = default;
    Input(const Input &) = delete;
    Input &operator=(const Input &) = delete;
    ~Input()
    {
        if (ctx)
            avformat_close_input(&ctx);
    }

    AVStream *st() const { return ctx->streams[stream]; }

    bool open(const QString &path, AVMediaType type, QString *error)
    {
        const QByteArray utf8 = path.toUtf8();
        if (avformat_open_input(&ctx, utf8.constData(), nullptr, nullptr) < 0) {
            ctx = nullptr;
            *error = QStringLiteral("Could not open %1").arg(QFileInfo(path).fileName());
            return false;
        }
        if (avformat_find_stream_info(ctx, nullptr) < 0) {
            *error = QStringLiteral("Could not read %1").arg(QFileInfo(path).fileName());
            return false;
        }
        stream = av_find_best_stream(ctx, type, -1, -1, nullptr, 0);
        if (stream < 0) {
            *error = QStringLiteral("%1 has no %2 stream")
                         .arg(QFileInfo(path).fileName(),
                              type == AVMEDIA_TYPE_VIDEO ? QStringLiteral("video")
                                                         : QStringLiteral("audio"));
            return false;
        }
        return true;
    }
};

// Segments of one export come from identically configured encoders; a mismatch would mean a
// file the joined stream cannot be decoded from with the first segment's headers.
bool sameVideoFormat(const AVCodecParameters *a, const AVCodecParameters *b)
{
    if (a->codec_id != b->codec_id || a->width != b->width || a->height != b->height
        || a->format != b->format || a->extradata_size != b->extradata_size)
        return false;
    return a->extradata_size == 0
           || std::memcmp(a->extradata, b->extradata, size_t(a->extradata_size)) == 0;
}

} // namespace

QList<Segment> planSegments(int64_t totalFrames, int fpsNum, int fpsDen, double segmentSeconds)
{
    QList<Segment> out;
    if (totalFrames <= 0)
        return out;
    const double fps = (fpsNum > 0 && fpsDen > 0) ? double(fpsNum) / double(fpsDen) : 30.0;
    const int64_t per = qMax<int64_t>(1, std::llround(qMax(0.001, segmentSeconds) * fps));
    for (int64_t first = 0; first < totalFrames; first += per)
        out.append(Segment{first, qMin(per, totalFrames - first)});
    if (out.size() > 1 && out.last().frameCount * 3 < per) {
        const int64_t tail = out.takeLast().frameCount;
        out.last().frameCount += tail;
    }
    return out;
}

QString defaultJobsRoot()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("export-resume"));
}

bool supports(const ExportSettings &settings, const QString &outputPath)
{
#ifdef Q_OS_ANDROID
    Q_UNUSED(settings);
    Q_UNUSED(outputPath);
    return false;
#else
    if (settings.gifExport || settings.audioOnly || outputPath.isEmpty())
        return false;
    // A URL scheme ("content:", "file:") is not a path the job can write segments beside. A
    // Windows drive letter ("C:") is one character long, so it never matches.
    const QUrl url(outputPath);
    return url.scheme().size() <= 1;
#endif
}

QString jobDirFor(const QString &jobsRoot, const QString &outputPath)
{
    const QString root = jobsRoot.isEmpty() ? defaultJobsRoot() : jobsRoot;
    QString key = QDir::cleanPath(QFileInfo(outputPath).absoluteFilePath());
#ifdef Q_OS_WIN
    key = key.toLower();
#endif
    const QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QDir(root).filePath(QString::fromLatin1(hash.left(16)));
}

bool run(const drift::Project &project, const ExportSettings &settings, const QString &outputPath,
         const Options &options, QString *errorOut, const Exporter::ProgressFn &onProgress)
{
    if (!supports(settings, outputPath))
        return Exporter::run(project, settings, outputPath, errorOut, onProgress);

    int fpsNum = 0;
    int fpsDen = 1;
    QString error;
    const int64_t totalFrames = Exporter::frameCount(project, settings, &fpsNum, &fpsDen, &error);
    if (totalFrames <= 0 || fpsNum <= 0 || fpsDen <= 0) // let the exporter report it
        return Exporter::run(project, settings, outputPath, errorOut, onProgress);

    const double seconds = double(totalFrames) * fpsDen / fpsNum;
    const QString dir = jobDirFor(options.jobsRoot, outputPath);
    QList<Segment> plan;
    QSet<int> done;
    bool audioDone = false;
    QJsonObject job;
    QString suffix = QFileInfo(outputPath).suffix().toLower();
    if (suffix.isEmpty())
        suffix = Exporter::defaultSuffix(
            Exporter::preferredContainer(settings.videoCodecId, settings.audioCodecId));

    if (options.resume) {
        job = readJob(dir);
        plan = segmentsFromJson(job.value(QStringLiteral("segments")).toArray());
        if (job.value(QStringLiteral("version")).toInt() != kJobVersion || plan.isEmpty()
            || static_cast<int64_t>(job.value(QStringLiteral("totalFrames")).toDouble()) != totalFrames
            || job.value(QStringLiteral("fpsNum")).toInt() != fpsNum
            || job.value(QStringLiteral("fpsDen")).toInt() != fpsDen) {
            if (errorOut)
                *errorOut = QStringLiteral("The unfinished export can't be resumed. Export it again.");
            return false;
        }
        suffix = job.value(QStringLiteral("suffix")).toString(suffix);
        done = doneFromJson(job.value(QStringLiteral("done")).toArray());
        audioDone = job.value(QStringLiteral("audioDone")).toBool();
    } else {
        // Short exports gain nothing from the round trip through segments.
        plan = planSegments(totalFrames, fpsNum, fpsDen, options.segmentSeconds);
        if (seconds < options.minDurationSeconds || plan.size() < 2)
            return Exporter::run(project, settings, outputPath, errorOut, onProgress);

        // A fresh export to this path replaces whatever an older one left behind.
        QDir(dir).removeRecursively();
        if (!QDir().mkpath(dir)) {
            if (errorOut)
                *errorOut = QStringLiteral("Could not create the export's temporary folder");
            return false;
        }
        QSaveFile projectFile(QDir(dir).filePath(kProjectFile));
        if (!projectFile.open(QIODevice::WriteOnly)) {
            if (errorOut)
                *errorOut = QStringLiteral("Could not write to the export's temporary folder");
            return false;
        }
        projectFile.write(QJsonDocument(project.toJson()).toJson(QJsonDocument::Compact));
        if (!projectFile.commit()) {
            if (errorOut)
                *errorOut = QStringLiteral("Could not write to the export's temporary folder");
            return false;
        }
        job = QJsonObject{
            {QStringLiteral("version"), kJobVersion},
            {QStringLiteral("outputPath"), outputPath},
            {QStringLiteral("outputUrl"), options.outputUrl},
            {QStringLiteral("settings"), QJsonObject::fromVariantMap(options.settingsMap)},
            {QStringLiteral("suffix"), suffix},
            {QStringLiteral("fpsNum"), fpsNum},
            {QStringLiteral("fpsDen"), fpsDen},
            {QStringLiteral("totalFrames"), double(totalFrames)},
            {QStringLiteral("segments"), segmentsToJson(plan)},
            {QStringLiteral("done"), QJsonArray{}},
            {QStringLiteral("audioDone"), false},
            {QStringLiteral("created"), QDateTime::currentDateTime().toString(Qt::ISODate)},
        };
    }
    job.insert(QStringLiteral("updated"), QDateTime::currentDateTime().toString(Qt::ISODate));
    writeJob(dir, job);

    // Bar: the segments take 94 %, the audio 3 %, the join the rest.
    bool userCancelled = false;
    const auto report = [&](double fraction) {
        if (!onProgress)
            return true;
        if (!onProgress(qBound(0.0, fraction, 1.0))) {
            userCancelled = true;
            return false;
        }
        return true;
    };
    const auto stop = [&](const QString &message) {
        if (userCancelled || isCancelled(message)) {
            discardJob(dir);
            if (errorOut)
                *errorOut = QStringLiteral("Export cancelled");
        } else if (errorOut) {
            // The job stays: the segments finished so far are kept for a resume.
            *errorOut = message.isEmpty() ? QStringLiteral("Export failed") : message;
        }
        return false;
    };

    const QDir jobDir(dir);
    int64_t framesDone = 0;
    for (int k = 0; k < plan.size(); ++k) {
        const QString finished = jobDir.filePath(segmentName(k, suffix));
        if (done.contains(k) && QFileInfo(finished).size() > 0) {
            framesDone += plan[k].frameCount;
            continue;
        }
        if (!report(0.94 * double(framesDone) / double(totalFrames)))
            return stop({});

        // Rendered under a .part name and renamed when complete, so a segment cut short by a crash
        // is never taken for a finished one.
        const QString part = jobDir.filePath(segmentPartName(k, suffix));
        QFile::remove(part);
        ExportSettings segmentSettings = settings;
        segmentSettings.segmentFirstFrame = plan[k].firstFrame;
        segmentSettings.segmentFrameCount = plan[k].frameCount;
        const int64_t before = framesDone;
        const int64_t count = plan[k].frameCount;
        QString segmentError;
        const bool ok = Exporter::run(project, segmentSettings, part, &segmentError,
                                      [&](double f) {
                                          return report(0.94 * (double(before) + f * double(count))
                                                        / double(totalFrames));
                                      });
        if (!ok)
            return stop(segmentError);
        QFile::remove(finished);
        if (!QFile::rename(part, finished))
            return stop(QStringLiteral("Could not keep a finished part of the export"));
        done.insert(k);
        framesDone += count;
        job.insert(QStringLiteral("done"), doneToJson(done));
        job.insert(QStringLiteral("updated"), QDateTime::currentDateTime().toString(Qt::ISODate));
        writeJob(dir, job);
    }

    // The whole range's audio in one piece: no encoder delay at the cuts, no drift.
    const QString audioSuffix = audioSuffixFor(suffix);
    const QString audioPath = jobDir.filePath(QStringLiteral("audio.") + audioSuffix);
    if (!audioDone || QFileInfo(audioPath).size() <= 0) {
        const QString audioPart = jobDir.filePath(QStringLiteral("audio.part.") + audioSuffix);
        QFile::remove(audioPart);
        ExportSettings audioSettings = settings;
        audioSettings.audioOnly = true;
        audioSettings.segmentFirstFrame = 0;
        audioSettings.segmentFrameCount = 0;
        QString audioError;
        if (!Exporter::run(project, audioSettings, audioPart, &audioError,
                           [&](double f) { return report(0.94 + 0.03 * f); }))
            return stop(audioError);
        QFile::remove(audioPath);
        if (!QFile::rename(audioPart, audioPath))
            return stop(QStringLiteral("Could not keep the export's audio"));
        job.insert(QStringLiteral("audioDone"), true);
        writeJob(dir, job);
    }

    QStringList segmentPaths;
    for (int k = 0; k < plan.size(); ++k)
        segmentPaths.append(jobDir.filePath(segmentName(k, suffix)));
    QString joinError;
    if (!joinSegments(segmentPaths, plan, fpsNum, fpsDen, audioPath, outputPath, &joinError,
                      [&](double f) { return report(0.97 + 0.03 * f); })) {
        if (userCancelled)
            return stop({});
        // The parts do not fit together (an encoder that changed its headers between parts, say).
        // Rather than hand over a broken file, export it once more in one pass.
        qWarning("SegmentedExport: join failed (%s); exporting in one pass",
                 qPrintable(joinError));
        QString singleError;
        if (!Exporter::run(project, settings, outputPath, &singleError, report))
            return stop(singleError);
    }

    discardJob(dir);
    report(1.0);
    return true;
}

QList<PendingJob> pendingJobs(const QString &jobsRoot)
{
    QList<PendingJob> jobs;
    const QDir root(jobsRoot.isEmpty() ? defaultJobsRoot() : jobsRoot);
    for (const QFileInfo &info : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time)) {
        const QJsonObject job = readJob(info.absoluteFilePath());
        if (job.value(QStringLiteral("version")).toInt() != kJobVersion)
            continue;
        PendingJob pending;
        pending.dir = info.absoluteFilePath();
        pending.outputPath = job.value(QStringLiteral("outputPath")).toString();
        pending.outputUrl = job.value(QStringLiteral("outputUrl")).toString();
        pending.segmentsTotal = int(job.value(QStringLiteral("segments")).toArray().size());
        pending.segmentsDone = int(doneFromJson(job.value(QStringLiteral("done")).toArray()).size());
        pending.updated = QDateTime::fromString(job.value(QStringLiteral("updated")).toString(),
                                                Qt::ISODate);
        if (pending.outputPath.isEmpty() || pending.segmentsTotal <= 0)
            continue;
        jobs.append(pending);
    }
    return jobs;
}

bool loadJob(const QString &dir, drift::Project *project, QVariantMap *settingsMap,
             QString *outputPath, QString *outputUrl, QString *errorOut)
{
    const QJsonObject job = readJob(dir);
    if (job.value(QStringLiteral("version")).toInt() != kJobVersion) {
        if (errorOut)
            *errorOut = QStringLiteral("The unfinished export can't be resumed. Export it again.");
        return false;
    }
    QFile file(QDir(dir).filePath(kProjectFile));
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorOut)
            *errorOut = QStringLiteral("The unfinished export can't be resumed. Export it again.");
        return false;
    }
    QString parseError;
    drift::Project loaded =
        drift::Project::fromJson(QJsonDocument::fromJson(file.readAll()).object(), &parseError);
    if (!parseError.isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("The unfinished export can't be resumed: %1").arg(parseError);
        return false;
    }
    // Media can be gone since the crash (a downloaded file cleaned up, a drive unplugged); the
    // export would render black where it was, so say which file instead.
    for (const drift::bundle::MediaEntry &entry : drift::bundle::collectMedia(loaded, false)) {
        if (QDir::isAbsolutePath(entry.originalPath) && !QFileInfo::exists(entry.originalPath)) {
            if (errorOut)
                *errorOut = QStringLiteral("Can't resume the export: %1 is missing")
                                .arg(QFileInfo(entry.originalPath).fileName());
            return false;
        }
    }
    if (project)
        *project = std::move(loaded);
    if (settingsMap)
        *settingsMap = job.value(QStringLiteral("settings")).toObject().toVariantMap();
    if (outputPath)
        *outputPath = job.value(QStringLiteral("outputPath")).toString();
    if (outputUrl)
        *outputUrl = job.value(QStringLiteral("outputUrl")).toString();
    return true;
}

void discardJob(const QString &dir)
{
    if (!dir.isEmpty())
        QDir(dir).removeRecursively();
}

bool joinSegments(const QStringList &segmentPaths, const QList<Segment> &segments, int fpsNum,
                  int fpsDen, const QString &audioPath, const QString &outputPath,
                  QString *errorOut, const std::function<bool(double)> &onProgress)
{
    QString error;
    const auto fail = [&](const QString &message) {
        if (errorOut)
            *errorOut = message;
        return false;
    };
    if (segmentPaths.isEmpty() || segmentPaths.size() != segments.size() || fpsNum <= 0
        || fpsDen <= 0)
        return fail(QStringLiteral("Nothing to join"));

    const AVRational frameTb{fpsDen, fpsNum};
    Input audio;
    if (!audio.open(audioPath, AVMEDIA_TYPE_AUDIO, &error))
        return fail(error);
    auto video = std::make_unique<Input>();
    if (!video->open(segmentPaths.first(), AVMEDIA_TYPE_VIDEO, &error))
        return fail(error);
    AVCodecParameters *firstPar = avcodec_parameters_alloc();
    avcodec_parameters_copy(firstPar, video->st()->codecpar);

    AVFormatContext *ofmt = nullptr;
    AVPacket *vp = av_packet_alloc();
    AVPacket *ap = av_packet_alloc();
    AVStream *ov = nullptr;
    AVStream *oa = nullptr;
    bool headerWritten = false;
    bool ok = false;
    int segIndex = 0;
    int64_t shift = 0;
    bool firstOfSegment = true;
    int64_t lastVideoDts = INT64_MIN;
    int64_t lastAudioDts = INT64_MIN;
    int videoState = 0;
    int audioState = 0;

    const QByteArray outUtf8 = outputPath.toUtf8();
    avformat_alloc_output_context2(&ofmt, nullptr, nullptr, outUtf8.constData());
    if (!ofmt)
        avformat_alloc_output_context2(&ofmt, nullptr, "matroska", outUtf8.constData());

    // Lines the next segment's first frame up with its place on the timeline. start_time is the
    // segment's earliest presentation time, so B-frame reordering at its head is kept as is.
    const auto alignSegment = [&]() {
        const AVStream *is = video->st();
        const int64_t start = is->start_time != AV_NOPTS_VALUE
                                  ? av_rescale_q(is->start_time, is->time_base, ov->time_base)
                                  : 0;
        shift = av_rescale_q(segments[segIndex].firstFrame, frameTb, ov->time_base) - start;
        firstOfSegment = true;
    };

    // 1 = a packet in vp, 0 = every segment read, -1 = error.
    const auto readVideo = [&]() -> int {
        for (;;) {
            const int rc = av_read_frame(video->ctx, vp);
            if (rc == AVERROR_EOF) {
                if (++segIndex >= segmentPaths.size())
                    return 0;
                video = std::make_unique<Input>();
                if (!video->open(segmentPaths.at(segIndex), AVMEDIA_TYPE_VIDEO, &error))
                    return -1;
                if (!sameVideoFormat(firstPar, video->st()->codecpar)) {
                    error = QStringLiteral("Part %1 of the export was encoded differently")
                                .arg(segIndex + 1);
                    return -1;
                }
                alignSegment();
                if (onProgress && !onProgress(double(segIndex) / double(segmentPaths.size()))) {
                    error = QStringLiteral("Export cancelled");
                    return -1;
                }
                continue;
            }
            if (rc < 0) {
                error = QStringLiteral("Could not read part %1 of the export").arg(segIndex + 1);
                return -1;
            }
            if (vp->stream_index != video->stream) {
                av_packet_unref(vp);
                continue;
            }
            if (firstOfSegment && !(vp->flags & AV_PKT_FLAG_KEY)) {
                error = QStringLiteral("Part %1 of the export does not start on a keyframe")
                            .arg(segIndex + 1);
                av_packet_unref(vp);
                return -1;
            }
            firstOfSegment = false;
            av_packet_rescale_ts(vp, video->st()->time_base, ov->time_base);
            if (vp->pts != AV_NOPTS_VALUE)
                vp->pts += shift;
            if (vp->dts != AV_NOPTS_VALUE) {
                vp->dts += shift;
                if (lastVideoDts != INT64_MIN && vp->dts <= lastVideoDts)
                    vp->dts = lastVideoDts + 1;
                if (vp->pts != AV_NOPTS_VALUE && vp->pts < vp->dts)
                    vp->pts = vp->dts;
                lastVideoDts = vp->dts;
            }
            vp->stream_index = ov->index;
            vp->pos = -1;
            return 1;
        }
    };

    const auto readAudio = [&]() -> int {
        for (;;) {
            const int rc = av_read_frame(audio.ctx, ap);
            if (rc == AVERROR_EOF)
                return 0;
            if (rc < 0) {
                error = QStringLiteral("Could not read the export's audio");
                return -1;
            }
            if (ap->stream_index != audio.stream) {
                av_packet_unref(ap);
                continue;
            }
            av_packet_rescale_ts(ap, audio.st()->time_base, oa->time_base);
            if (ap->dts != AV_NOPTS_VALUE) {
                if (lastAudioDts != INT64_MIN && ap->dts <= lastAudioDts)
                    ap->dts = lastAudioDts + 1;
                if (ap->pts != AV_NOPTS_VALUE && ap->pts < ap->dts)
                    ap->pts = ap->dts;
                lastAudioDts = ap->dts;
            }
            ap->stream_index = oa->index;
            ap->pos = -1;
            return 1;
        }
    };

    if (!ofmt || !vp || !ap || !firstPar) {
        error = QStringLiteral("Could not determine output format");
        goto cleanup;
    }

    ov = avformat_new_stream(ofmt, nullptr);
    oa = avformat_new_stream(ofmt, nullptr);
    if (!ov || !oa || avcodec_parameters_copy(ov->codecpar, video->st()->codecpar) < 0
        || avcodec_parameters_copy(oa->codecpar, audio.st()->codecpar) < 0) {
        error = QStringLiteral("Could not create output streams");
        goto cleanup;
    }
    ov->codecpar->codec_tag = 0;
    oa->codecpar->codec_tag = 0;
    ov->time_base = frameTb;
    ov->avg_frame_rate = AVRational{fpsNum, fpsDen};
    ov->r_frame_rate = ov->avg_frame_rate;
    oa->time_base = audio.st()->time_base;

    if (!(ofmt->oformat->flags & AVFMT_NOFILE)
        && avio_open(&ofmt->pb, outUtf8.constData(), AVIO_FLAG_WRITE) < 0) {
        error = QStringLiteral("Could not open the output file");
        goto cleanup;
    }
    if (avformat_write_header(ofmt, nullptr) < 0) {
        error = QStringLiteral("Could not write the file header");
        goto cleanup;
    }
    headerWritten = true;
    // The muxer may have picked its own time bases; shifts are computed in those.
    alignSegment();

    videoState = readVideo();
    audioState = videoState < 0 ? 0 : readAudio();
    if (videoState < 0 || audioState < 0)
        goto cleanup;

    // Interleaved by decode time here, so the muxer never has to buffer one stream's whole length
    // while it waits for the other.
    while (videoState == 1 || audioState == 1) {
        bool takeVideo = videoState == 1;
        if (videoState == 1 && audioState == 1) {
            const int64_t vt = vp->dts != AV_NOPTS_VALUE ? vp->dts : vp->pts;
            const int64_t at = ap->dts != AV_NOPTS_VALUE ? ap->dts : ap->pts;
            takeVideo = av_compare_ts(vt, ov->time_base, at, oa->time_base) <= 0;
        }
        if (av_interleaved_write_frame(ofmt, takeVideo ? vp : ap) < 0) {
            error = QStringLiteral("Could not write the export");
            goto cleanup;
        }
        if (takeVideo)
            videoState = readVideo();
        else
            audioState = readAudio();
        if (videoState < 0 || audioState < 0)
            goto cleanup;
    }

    if (av_write_trailer(ofmt) < 0) {
        error = QStringLiteral("Could not finalize the file");
        goto cleanup;
    }
    ok = true;

cleanup:
    av_packet_free(&vp);
    av_packet_free(&ap);
    avcodec_parameters_free(&firstPar);
    if (ofmt) {
        if (headerWritten && !ok && ofmt->pb)
            av_write_trailer(ofmt);
        if (ofmt->pb && !(ofmt->oformat->flags & AVFMT_NOFILE))
            avio_closep(&ofmt->pb);
        avformat_free_context(ofmt);
    }
    if (!ok) {
        QFile::remove(outputPath);
        if (errorOut)
            *errorOut = error.isEmpty() ? QStringLiteral("Could not join the export") : error;
    }
    return ok;
}

} // namespace SegmentedExport
