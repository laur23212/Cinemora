#include "app/jobs/audio/AudioToolsController.h"

#include "app/editor/AppController.h"
#include "app/editor/appcontroller/AppControllerInternal.h"
#include "core/edit/CutOps.h"
#include "core/edit/TimelineOps.h"
#include "engine/audio/AudioMixer.h"
#include "engine/audio/dsp/FfmpegAudioFilter.h"
#include "engine/decode/readers/ClipReaderPool.h"
#include "engine/encode/writers/AudioFileWriter.h"

#include <QFile>
#include <QMetaObject>
#include <QUuid>
#include <QVariantMap>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

// Loudness and noise reduction work at the playback path's own rate; Silence remover only needs
// to tell sound from quiet.
constexpr int kRenderRate = 48000;
constexpr int kSilenceRate = 16000;
constexpr quint64 kAudioToolsScanStreamId = 0xA5'11'5C'A4'00'00'00'21ull;
// The ramp each cut edge gets, as Remove silence in the clip menu does, so a cut never clicks.
constexpr drift::TimeUs kDeclickUs = 30'000;
// A silence shorter than this after the padding is taken off isn't worth a cut.
constexpr drift::TimeUs kMinCutUs = 50'000;

bool carriesAudio(const drift::Clip &clip)
{
    return clip.type == drift::ClipType::Audio
           || (clip.type == drift::ClipType::Video && !clip.suppressEmbeddedAudio);
}

// The clip whose sound `clip` plays: itself, or for a video whose audio was separated, its audio
// partner. (The same rule as Remove silence and Sync audio.)
const drift::Clip *audibleClip(const drift::Project &project, const drift::Clip &clip)
{
    if (carriesAudio(clip))
        return &clip;
    for (const drift::ClipRef &ref : drift::linkedPartners(project, clip)) {
        if (project.tracks().at(ref.trackIndex).type != drift::TrackType::Audio)
            continue;
        const drift::Clip &partner = project.tracks().at(ref.trackIndex).clips.at(ref.clipIndex);
        if (carriesAudio(partner))
            return &partner;
    }
    return nullptr;
}

bool findClip(const drift::Project &project, const QString &id, int *trackOut, int *clipOut)
{
    for (int t = 0; t < project.tracks().size(); ++t) {
        const QList<drift::Clip> &clips = project.tracks().at(t).clips;
        for (int c = 0; c < clips.size(); ++c) {
            if (clips.at(c).id == id) {
                *trackOut = t;
                *clipOut = c;
                return true;
            }
        }
    }
    return false;
}

} // namespace

AudioToolsController::AudioToolsController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
}

AudioToolsController::~AudioToolsController()
{
    m_cancel.storeRelaxed(1);
    if (m_future.isRunning())
        m_future.waitForFinished();
}

void AudioToolsController::cancel()
{
    if (m_running)
        m_cancel.storeRelaxed(1);
}

bool AudioToolsController::begin(const QString &message)
{
    if (m_running) {
        m_app->setLastMessage(tr("An audio tool is already running"), QStringLiteral("warning"));
        return false;
    }
    // Same reason as the other audio jobs: playback would drive the decode pool from a second
    // thread while this walks it.
    m_app->setPlaying(false);
    m_cancel.storeRelaxed(0);
    m_progress = 0.0;
    emit progressChanged();
    m_status = message;
    emit statusChanged();
    m_running = true;
    emit runningChanged();
    m_app->setLastMessage(message);
    return true;
}

void AudioToolsController::end()
{
    m_running = false;
    emit runningChanged();
    m_progress = 1.0;
    emit progressChanged();
}

void AudioToolsController::reportProgress(double fraction, const QString &status)
{
    QMetaObject::invokeMethod(
        this,
        [this, fraction, status]() {
            if (!m_running)
                return;
            m_progress = std::clamp(fraction, 0.0, 1.0);
            emit progressChanged();
            if (!status.isEmpty() && status != m_status) {
                m_status = status;
                emit statusChanged();
            }
        },
        Qt::QueuedConnection);
}

bool AudioToolsController::canProcess(int trackIndex, int clipIndex) const
{
    if (!m_app->isValidClipIndex(trackIndex, clipIndex))
        return false;
    const drift::Clip &clip = m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex);
    const drift::Clip *audible = audibleClip(m_app->m_project, clip);
    return audible && !audible->path.isEmpty() && audible->sequenceId.isEmpty()
           && audible->srcOut > audible->srcIn;
}

QString AudioToolsController::clipIdAt(int trackIndex, int clipIndex) const
{
    if (!m_app->isValidClipIndex(trackIndex, clipIndex))
        return {};
    const drift::Clip &clip = m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex);
    const drift::Clip *audible = audibleClip(m_app->m_project, clip);
    return audible ? audible->id : QString();
}

// ---- Loudness and noise reduction --------------------------------------------------------

void AudioToolsController::normalizeLoudness(int trackIndex, int clipIndex, double targetLufs)
{
    startRender(trackIndex, clipIndex, RenderKind::Loudness, targetLufs);
}

void AudioToolsController::reduceNoise(int trackIndex, int clipIndex, double strength)
{
    startRender(trackIndex, clipIndex, RenderKind::Noise, strength);
}

void AudioToolsController::startRender(int trackIndex, int clipIndex, RenderKind kind, double amount)
{
    if (!canProcess(trackIndex, clipIndex)) {
        m_app->setLastMessage(tr("Select a clip with audio"), QStringLiteral("warning"));
        return;
    }
    const drift::Clip &target = m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex);
    const drift::Clip audible = *audibleClip(m_app->m_project, target);
    const QString filter = kind == RenderKind::Loudness ? QStringLiteral("loudnorm") : QStringLiteral("afftdn");
    if (!drift::FfmpegAudioFilter::hasFilter(filter.toLatin1().constData())) {
        m_app->setLastMessage(tr("This FFmpeg build has no %1 filter").arg(filter), QStringLiteral("error"));
        return;
    }
    const QString outPath = drift::newDenoisePath();
    if (outPath.isEmpty()) {
        m_app->setLastMessage(tr("Could not create an output file"), QStringLiteral("error"));
        return;
    }
    if (!begin(kind == RenderKind::Loudness ? tr("Normalizing loudness…") : tr("Reducing noise…")))
        return;

    const QString path = audible.path;
    const QString clipId = audible.id;
    const int ordinal = audible.audioStreamIndex;
    const drift::TimeUs srcIn = audible.srcIn;
    const drift::TimeUs srcOut = audible.srcOut;

    m_future = QtConcurrent::run([this, path, clipId, ordinal, srcIn, srcOut, kind, amount, outPath]() {
        const int64_t total = (srcOut - srcIn) * kRenderRate / drift::kUsPerSecond;
        // Each pass decodes the clip's source window afresh, in the source's own time base; speed
        // and reverse stay on the clip, so the new file lines up with the original.
        const auto makeFeed = [&](double from, double to) {
            auto read = std::make_shared<int64_t>(0);
            return [this, path, ordinal, srcIn, total, read, from, to](float *dst, int maxFrames) {
                if (m_cancel.loadRelaxed() != 0)
                    return -1;
                const int want = int(std::min<int64_t>(maxFrames, total - *read));
                if (want <= 0)
                    return 0;
                const drift::TimeUs at = srcIn + drift::TimeUs(*read * drift::kUsPerSecond / kRenderRate);
                int got = ClipReaderPool::instance().readAudioInterleaved(
                    path, kAudioToolsScanStreamId, at, want, kRenderRate, dst, ordinal);
                if (got <= 0) {
                    // Past the decodable end: the rest is silence, so the render keeps the clip's length.
                    std::fill(dst, dst + size_t(want) * 2, 0.0f);
                    got = want;
                }
                *read += got;
                reportProgress(from + (to - from) * double(*read) / double(std::max<int64_t>(1, total)));
                return got;
            };
        };

        QString error;
        QString chain;
        double writeFrom = 0.0;
        if (kind == RenderKind::Loudness) {
            reportProgress(0.0, tr("Measuring loudness…"));
            const drift::LoudnessStats stats =
                drift::measureEbuR128(makeFeed(0.0, 0.5), kRenderRate, 2, &error);
            if (stats.ok) {
                chain = drift::loudnormChain(amount, -1.0, 11.0, stats);
                writeFrom = 0.5;
                reportProgress(0.5, tr("Normalizing loudness…"));
            }
        } else {
            chain = drift::afftdnChain(amount);
        }

        bool ok = !chain.isEmpty();
        if (ok) {
            drift::AudioFileWriter writer;
            ok = writer.open(outPath, kRenderRate, 2, &error);
            if (ok) {
                ok = drift::streamAudioFilter(
                    kRenderRate, 2, chain, makeFeed(writeFrom, 1.0),
                    [&](const float *data, int frames) { return writer.writeFrames(data, frames, &error); },
                    &error);
                ok = ok && writer.finish(&error);
                if (!ok)
                    writer.abort();
            }
        }
        const bool cancelled = m_cancel.loadRelaxed() != 0;
        QMetaObject::invokeMethod(
            this,
            [this, ok, cancelled, error, outPath, clipId, srcIn, srcOut, kind]() {
                end();
                if (!ok) {
                    QFile::remove(outPath);
                    const QString message = cancelled ? tr("Cancelled") : error;
                    m_status = message;
                    emit statusChanged();
                    m_app->setLastMessage(message, cancelled ? QStringLiteral("info") : QStringLiteral("error"));
                    emit finished(false, message);
                    return;
                }
                finishRender(clipId, outPath, srcIn, srcOut, kind);
            },
            Qt::QueuedConnection);
    });
}

void AudioToolsController::finishRender(const QString &clipId, const QString &audioPath,
                                        drift::TimeUs srcIn, drift::TimeUs srcOut, RenderKind kind)
{
    const QString label = kind == RenderKind::Loudness ? tr("Normalize loudness") : tr("Reduce noise");
    int track = -1, index = -1;
    if (!findClip(m_app->m_project, clipId, &track, &index)
        || m_app->m_project.tracks().at(track).clips.at(index).srcIn != srcIn
        || m_app->m_project.tracks().at(track).clips.at(index).srcOut != srcOut) {
        // Deleted or trimmed while the job ran: the render no longer matches the clip.
        QFile::remove(audioPath);
        const QString message = tr("The clip changed while it was processed");
        m_app->setLastMessage(message, QStringLiteral("warning"));
        emit finished(false, message);
        return;
    }

    const drift::Project before = m_app->m_project;
    // A video still playing its own sound gets that sound separated onto an audio track first;
    // the new file then replaces the separated clip's media, and the picture is left untouched.
    if (m_app->m_project.tracks().at(track).clips.at(index).type == drift::ClipType::Video) {
        if (!drift::appcontroller_detail::detachEmbeddedAudioFromVideo(m_app->m_project, m_app->m_assetLibrary,
                                                                     track, index)) {
            m_app->m_project = before;
            QFile::remove(audioPath);
            const QString message = tr("Could not separate the video's audio");
            m_app->setLastMessage(message, QStringLiteral("error"));
            emit finished(false, message);
            return;
        }
        // Separating inserts a track, which can shift indices: look the video up again by id.
        const drift::Clip *companion = nullptr;
        if (findClip(m_app->m_project, clipId, &track, &index))
            companion = audibleClip(m_app->m_project, m_app->m_project.tracks().at(track).clips.at(index));
        if (!companion || !findClip(m_app->m_project, companion->id, &track, &index)) {
            m_app->m_project = before;
            QFile::remove(audioPath);
            const QString message = tr("Could not separate the video's audio");
            m_app->setLastMessage(message, QStringLiteral("error"));
            emit finished(false, message);
            return;
        }
    }

    // Everything about the clip stays (timing, link, volume dots, fades, effects) except the media:
    // srcIn/srcOut rebase because the render covers exactly the old source window.
    drift::Clip &clip = m_app->m_project.tracks()[track].clips[index];
    clip.path = audioPath;
    clip.assetId.clear();
    clip.audioStreamIndex = 0;
    clip.srcIn = 0;
    clip.srcOut = srcOut - srcIn;
    const QString base = clip.name.isEmpty() ? tr("Clip") : clip.name;
    const QString suffix = kind == RenderKind::Loudness ? tr(" (normalized)") : tr(" (noise reduced)");
    if (!base.endsWith(suffix))
        clip.name = base + suffix;

    const QString done = kind == RenderKind::Loudness ? tr("Loudness normalized") : tr("Noise reduced");
    m_app->pushProjectEdit(before, label);
    m_app->finishEdit(done);
    m_status = done;
    emit statusChanged();
    m_app->setLastMessage(done, QStringLiteral("success"));
    emit finished(true, done);
}

// ---- Silence remover ---------------------------------------------------------------------

double AudioToolsController::silencePreviewSeconds() const
{
    double total = 0.0;
    for (const QVariant &v : m_preview) {
        const QVariantMap r = v.toMap();
        total += r.value(QStringLiteral("end")).toDouble() - r.value(QStringLiteral("start")).toDouble();
    }
    return total;
}

void AudioToolsController::clearSilencePreview()
{
    if (m_preview.isEmpty() && m_previewClipId.isEmpty())
        return;
    m_preview.clear();
    m_previewClipId.clear();
    m_previewStartUs = 0;
    m_previewDurationUs = 0;
    emit silencePreviewChanged();
}

void AudioToolsController::findSilence(int trackIndex, int clipIndex, double thresholdDb,
                                       double minSeconds, double paddingSeconds)
{
    if (!canProcess(trackIndex, clipIndex)) {
        m_app->setLastMessage(tr("Select a clip with audio"), QStringLiteral("warning"));
        return;
    }
    if (!drift::FfmpegAudioFilter::hasFilter("silencedetect")) {
        m_app->setLastMessage(tr("This FFmpeg build has no %1 filter").arg(QStringLiteral("silencedetect")),
                              QStringLiteral("error"));
        return;
    }
    const drift::Clip &target = m_app->m_project.tracks().at(trackIndex).clips.at(clipIndex);
    const drift::Clip &audible = *audibleClip(m_app->m_project, target);
    const QString clipId = audible.id;
    const drift::TimeUs startUs = audible.timelineStart;
    const drift::TimeUs durationUs = audible.timelineDuration;
    if (durationUs <= 0)
        return;
    // The clip as it plays: its speed, reverse and volume dots included, every other clip muted.
    drift::Project snap = m_app->m_project.detachedCopy();
    drift::muteAllButClip(snap, clipId);
    clearSilencePreview();
    if (!begin(tr("Finding silence…")))
        return;

    const double threshold = std::clamp(thresholdDb, -80.0, -10.0);
    const double minLen = std::clamp(minSeconds, 0.1, 10.0);
    const drift::TimeUs padUs = drift::secondsToUs(std::clamp(paddingSeconds, 0.0, 1.0));

    m_future = QtConcurrent::run([this, snap, clipId, startUs, durationUs, threshold, minLen, padUs]() {
        const int64_t total = durationUs * kSilenceRate / drift::kUsPerSecond;
        AudioMixer mixer;
        mixer.setProject(&snap);
        int64_t done = 0;
        const auto feed = [&](float *dst, int maxFrames) {
            if (m_cancel.loadRelaxed() != 0)
                return -1;
            const int n = int(std::min<int64_t>(maxFrames, total - done));
            if (n <= 0)
                return 0;
            std::fill(dst, dst + size_t(n) * 2, 0.0f);
            mixer.mix(startUs + done * drift::kUsPerSecond / kSilenceRate, n, kSilenceRate, dst);
            done += n;
            reportProgress(double(done) / double(std::max<int64_t>(1, total)));
            return n;
        };
        QString error;
        const QList<drift::SilenceSpanSeconds> spans =
            drift::detectSilenceFfmpeg(feed, kSilenceRate, 2, threshold, minLen, &error);
        const bool cancelled = m_cancel.loadRelaxed() != 0;

        // Padding keeps a little quiet either side of the sound, except at the clip's own edges,
        // where there is no sound to protect.
        QVariantList ranges;
        for (const drift::SilenceSpanSeconds &s : spans) {
            drift::TimeUs a = drift::secondsToUs(s.start);
            drift::TimeUs b = std::min(durationUs, drift::secondsToUs(s.end));
            if (a > 0)
                a += padUs;
            if (b < durationUs)
                b -= padUs;
            if (b - a < kMinCutUs)
                continue;
            ranges.append(QVariantMap{{QStringLiteral("start"), drift::usToSeconds(a)},
                                      {QStringLiteral("end"), drift::usToSeconds(b)}});
        }
        QMetaObject::invokeMethod(
            this,
            [this, cancelled, error, ranges, clipId, startUs, durationUs]() {
                end();
                if (cancelled || !error.isEmpty()) {
                    const QString message = cancelled ? tr("Cancelled") : error;
                    m_status = message;
                    emit statusChanged();
                    m_app->setLastMessage(message, cancelled ? QStringLiteral("info") : QStringLiteral("error"));
                    emit finished(false, message);
                    return;
                }
                m_preview = ranges;
                m_previewClipId = clipId;
                m_previewStartUs = startUs;
                m_previewDurationUs = durationUs;
                emit silencePreviewChanged();
                const QString message = ranges.isEmpty()
                    ? tr("No silence found")
                    : tr("%n silent part(s) found, %1 s in all", nullptr, int(ranges.size()))
                          .arg(silencePreviewSeconds(), 0, 'f', 1);
                m_status = message;
                emit statusChanged();
                m_app->setLastMessage(message);
                emit finished(true, message);
            },
            Qt::QueuedConnection);
    });
}

void AudioToolsController::seekToSilence(int index)
{
    if (index < 0 || index >= m_preview.size())
        return;
    const double start = m_preview.at(index).toMap().value(QStringLiteral("start")).toDouble();
    m_app->setPlayheadSeconds(std::max(0.0, drift::usToSeconds(m_previewStartUs) + start - 0.5));
}

void AudioToolsController::applySilenceRemoval()
{
    if (m_running || m_preview.isEmpty())
        return;
    int track = -1, index = -1;
    if (!findClip(m_app->m_project, m_previewClipId, &track, &index)) {
        clearSilencePreview();
        m_app->setLastMessage(tr("The clip changed since the silence was found"), QStringLiteral("warning"));
        return;
    }
    const drift::Clip clip = m_app->m_project.tracks().at(track).clips.at(index);
    // The preview is in the clip's timeline span; any move or trim since makes it point at the
    // wrong sound, so it has to be found again.
    if (clip.timelineStart != m_previewStartUs || clip.timelineDuration != m_previewDurationUs) {
        clearSilencePreview();
        m_app->setLastMessage(tr("The clip changed since the silence was found"), QStringLiteral("warning"));
        return;
    }

    QList<drift::TimeRangeUs> removed;
    for (const QVariant &v : std::as_const(m_preview)) {
        const QVariantMap r = v.toMap();
        removed.append({m_previewStartUs + drift::secondsToUs(r.value(QStringLiteral("start")).toDouble()),
                        m_previewStartUs + drift::secondsToUs(r.value(QStringLiteral("end")).toDouble())});
    }
    const QList<drift::TimeRangeUs> kept = drift::keptTimelineIntervals(
        clip, removed, std::max<drift::TimeUs>(drift::kCutMinEdgeUs, 2 * kDeclickUs));
    if (kept.isEmpty()) {
        m_app->setLastMessage(tr("That would remove the whole clip"), QStringLiteral("warning"));
        return;
    }
    drift::TimeUs keptTotal = 0;
    for (const drift::TimeRangeUs &k : kept)
        keptTotal += k.length();
    if (keptTotal >= clip.timelineDuration) {
        clearSilencePreview();
        m_app->setLastMessage(tr("No silence found"));
        return;
    }

    // The clip and every linked partner (a video and its separated sound) are cut at the same
    // places, and what follows on their tracks moves up to close the gaps: the clip-menu Remove
    // silence's own path.
    const drift::Project before = m_app->m_project;
    QString error;
    if (!m_app->replaceClipGroupWithSegments(
            track, index,
            [&kept](const drift::Clip &member) { return drift::packedSegments(member, kept, kDeclickUs); },
            true, nullptr, &error)) {
        m_app->m_project = before;
        m_app->setLastMessage(tr("Could not remove the silence"), QStringLiteral("error"));
        return;
    }
    clearSilencePreview();
    m_app->pushProjectEdit(before, tr("Remove silence"));
    m_app->finishEdit(tr("Remove silence"));
    const QString message =
        tr("Removed %1 s of silence").arg(drift::usToSeconds(clip.timelineDuration - keptTotal), 0, 'f', 1);
    m_status = message;
    emit statusChanged();
    m_app->setLastMessage(message, QStringLiteral("success"));
    emit finished(true, message);
}

#include "moc_AudioToolsController.cpp"
