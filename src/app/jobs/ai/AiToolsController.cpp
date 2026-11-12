#include "app/jobs/ai/AiToolsController.h"

#include "core/base/SafeMode.h"

#include "app/editor/AppController.h"
#include "app/editor/ProjectFileController.h"
#include "app/jobs/JobRegistry.h"
#include "app/mcp/McpController.h"
#include "core/edit/CutOps.h"
#include "core/edit/TimelineOps.h"
#include "core/project/Project.h"
#include "core/project/TranscriptEdit.h"
#include "engine/ml/speech/SpeechAudio.h"
#include "engine/ml/speech/diarization/SpeakerDiarizer.h"
#include "engine/ml/speech/transcription/CtcAligner.h"
#include "engine/ml/speech/transcription/LocalTranscription.h"
#include "engine/ml/speech/transcription/WhisperTranscriber.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <QUndoStack>

#include <algorithm>
#include <memory>
#include <utility>

namespace {

// m:ss, or h:mm:ss from an hour on.
QString timeLabel(drift::TimeUs us)
{
    const qint64 total = std::max<qint64>(0, us / drift::kUsPerSecond);
    const qint64 h = total / 3600;
    const qint64 m = (total / 60) % 60;
    const qint64 s = total % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

bool isMediaClip(const drift::Clip &clip)
{
    return (clip.type == drift::ClipType::Video || clip.type == drift::ClipType::Audio) && !clip.path.isEmpty();
}

// The text a line was listed with, read back from the transcript, to tell a stale line index.
QString speechText(const drift::Transcript &transcript, int first, int last)
{
    QStringList parts;
    for (int i = std::max(0, first); i <= last && i < transcript.words.size(); ++i) {
        const drift::TranscriptWord &w = transcript.words.at(i);
        const QString text = w.text.trimmed();
        if (drift::isSpeechToken(w) && !text.isEmpty())
            parts.append(text);
    }
    return parts.join(QLatin1Char(' '));
}

} // namespace

AiToolsController::AiToolsController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(0);
    connect(m_refreshTimer, &QTimer::timeout, this, &AiToolsController::refresh);

    // Selection, edits, undo and a new project all change which clip is in focus or what its
    // transcript lines map to on the timeline. Bursts collapse into one refresh.
    connect(m_app, &AppController::selectionChanged, this, &AiToolsController::scheduleRefresh);
    connect(m_app, &AppController::tracksChanged, this, &AiToolsController::scheduleRefresh);
    connect(&m_app->m_undoStack, &QUndoStack::indexChanged, this, &AiToolsController::scheduleRefresh);
    connect(m_app, &AppController::projectReset, this, [this] {
        m_lastCaptionedClipId.clear();
        scheduleRefresh();
    });
}

void AiToolsController::setTranscriptVisible(bool visible)
{
    if (m_transcriptVisible == visible)
        return;
    m_transcriptVisible = visible;
    emit transcriptVisibleChanged();
    if (visible)
        refresh();
}

void AiToolsController::scheduleRefresh()
{
    if (!m_refreshTimer->isActive())
        m_refreshTimer->start();
}

void AiToolsController::refresh()
{
    m_refreshTimer->stop();
    const drift::Project &project = m_app->m_project;

    QList<QPair<int, int>> pairs = m_app->m_selection;
    if (pairs.isEmpty() && m_app->m_selectedTrack >= 0 && m_app->m_selectedClip >= 0)
        pairs.append(qMakePair(m_app->m_selectedTrack, m_app->m_selectedClip));
    bool canCaption = false;
    for (const QPair<int, int> &pair : std::as_const(pairs)) {
        if (m_app->isValidClipIndex(pair.first, pair.second)
            && isMediaClip(project.tracks().at(pair.first).clips.at(pair.second))) {
            canCaption = true;
            break;
        }
    }
    if (canCaption != m_canCaption) {
        m_canCaption = canCaption;
        emit canCaptionChanged();
    }

    // Lines are only worked out while the panel shows them.
    if (!m_transcriptVisible)
        return;

    // The clip in focus: the selected video/audio clip, else the one captioned last (selecting the
    // new caption clip keeps its transcript up).
    const drift::Clip *target = nullptr;
    if (m_app->isValidClipIndex(m_app->m_selectedTrack, m_app->m_selectedClip)) {
        const drift::Clip &clip = project.tracks().at(m_app->m_selectedTrack).clips.at(m_app->m_selectedClip);
        if (isMediaClip(clip))
            target = &clip;
    }
    if (!target && !m_lastCaptionedClipId.isEmpty()) {
        const QVariantList at = m_app->locateClip(m_lastCaptionedClipId);
        if (at.size() == 2)
            target = &project.tracks().at(at.at(0).toInt()).clips.at(at.at(1).toInt());
    }

    QVariantMap info;
    QList<std::pair<Line, QVariantMap>> listed;
    QString assetId;
    QString sourceKey;
    if (target) {
        assetId = target->assetId;
        sourceKey = drift::subtitleSourceKey(*target);
        const drift::MediaAsset *asset = assetId.isEmpty() ? nullptr : project.asset(assetId);
        QString name = target->name;
        if (name.isEmpty() && asset)
            name = asset->name;
        if (name.isEmpty())
            name = QFileInfo(target->path).fileName();
        const drift::TranscriptPtr transcript = asset ? project.transcript(assetId) : nullptr;
        const bool usable = transcript && transcript->source.matches(target->path);
        info.insert(QStringLiteral("clipId"), target->id);
        info.insert(QStringLiteral("clipName"), name);
        info.insert(QStringLiteral("hasTranscript"), usable);
        if (usable) {
            info.insert(QStringLiteral("language"), transcript->language);
            info.insert(QStringLiteral("aligned"), transcript->wordTimingsAligned);
            info.insert(QStringLiteral("words"), transcript->speechTokenCount());
            const drift::TimeUs inUs = std::min(target->srcIn, target->srcOut);
            const drift::TimeUs outUs = std::max(target->srcIn, target->srcOut);
            for (const drift::TranscriptLine &l : drift::transcriptLines(*transcript, inUs, outUs)) {
                drift::TimeRangeUs on;
                if (!drift::sourceRangeToTimeline(*target, {l.startUs, l.endUs}, on))
                    continue;
                QString speaker;
                if (transcript->diarized && l.speaker >= 0 && l.speaker < transcript->speakers.size())
                    speaker = transcript->speakers.at(l.speaker).label;
                const Line line{l.firstWord, l.lastWord, l.startUs, l.endUs, on.startUs, l.text};
                listed.append({line,
                               QVariantMap{{QStringLiteral("text"), l.text},
                                           {QStringLiteral("start"), drift::usToSeconds(on.startUs)},
                                           {QStringLiteral("end"), drift::usToSeconds(on.endUs)},
                                           {QStringLiteral("time"), timeLabel(on.startUs)},
                                           {QStringLiteral("speaker"), speaker}}});
            }
            // Timeline order: a reversed clip says its source backwards.
            std::stable_sort(listed.begin(), listed.end(), [](const auto &a, const auto &b) {
                return a.first.timelineStartUs < b.first.timelineStartUs;
            });
        }
    }

    QList<Line> lines;
    QVariantList rows;
    lines.reserve(listed.size());
    rows.reserve(listed.size());
    for (const auto &entry : std::as_const(listed)) {
        lines.append(entry.first);
        rows.append(entry.second);
    }

    const QString clipId = target ? target->id : QString();
    const bool changed = clipId != m_targetClipId || info != m_info || rows != m_lineRows;
    m_targetClipId = clipId;
    m_targetAssetId = assetId;
    m_targetSourceKey = sourceKey;
    m_lines = lines;
    if (changed) {
        m_info = info;
        m_lineRows = rows;
        emit transcriptChanged();
    }
}

void AiToolsController::setCaptioning(bool captioning)
{
    if (m_captioning == captioning)
        return;
    m_captioning = captioning;
    emit captioningChanged();
}

void AiToolsController::setCaptionProgress(double progress)
{
    if (qFuzzyCompare(m_captionProgress + 1.0, progress + 1.0))
        return;
    m_captionProgress = progress;
    emit captionProgressChanged();
}

void AiToolsController::setCaptionStatus(const QString &status)
{
    if (m_captionStatus == status)
        return;
    m_captionStatus = status;
    emit captionStatusChanged();
}

bool AiToolsController::createCaptions(const QString &language, int maxWordsPerCue)
{
    if (drift::safemode::active()) {
        m_app->setLastMessage(tr("AI tools are off in safe mode. Restart normally to use them."),
                              QStringLiteral("warning"));
        return false;
    }
    if (m_captioning) {
        m_app->setLastMessage(tr("Captions are already being made"), QStringLiteral("warning"));
        return false;
    }
    const drift::Project &project = m_app->m_project;

    QList<QPair<int, int>> pairs = m_app->m_selection;
    if (pairs.isEmpty() && m_app->m_selectedTrack >= 0 && m_app->m_selectedClip >= 0)
        pairs.append(qMakePair(m_app->m_selectedTrack, m_app->m_selectedClip));

    // A linked video and its sound say the same thing over the same span: keep one, the sound, as
    // AppController::generateSubtitlesForClips does. Titles or captions in the selection are left
    // out rather than refused.
    QList<drift::Clip> clips;
    for (const QPair<int, int> &pair : std::as_const(pairs)) {
        if (!m_app->isValidClipIndex(pair.first, pair.second))
            continue;
        const drift::Clip &clip = project.tracks().at(pair.first).clips.at(pair.second);
        if (!isMediaClip(clip))
            continue;
        auto partner = std::find_if(clips.begin(), clips.end(), [&clip](const drift::Clip &taken) {
            return !clip.linkId.isEmpty() && taken.linkId == clip.linkId;
        });
        if (partner == clips.end())
            clips.append(clip);
        else if (clip.type == drift::ClipType::Audio)
            *partner = clip;
    }
    if (clips.isEmpty()) {
        m_app->setLastMessage(tr("Select a video or audio clip with speech on the timeline first"),
                              QStringLiteral("warning"));
        return false;
    }
    std::sort(clips.begin(), clips.end(), [](const drift::Clip &a, const drift::Clip &b) {
        return a.timelineStart < b.timelineStart;
    });
    // Checked now rather than after a long transcription: the caption clip refuses them anyway.
    for (int i = 1; i < clips.size(); ++i) {
        if (clips.at(i).timelineStart < clips.at(i - 1).timelineEnd()) {
            m_app->setLastMessage(tr("These clips overlap in time — caption them separately"),
                                  QStringLiteral("warning"));
            return false;
        }
    }
    const QString lang = language.trimmed().toLower();
    struct Work
    {
        QString assetId;
        QString path;
    };
    QList<Work> work;
    QStringList seen;
    for (const drift::Clip &clip : std::as_const(clips)) {
        const drift::MediaAsset *asset = clip.assetId.isEmpty() ? nullptr : project.asset(clip.assetId);
        const QString name = asset && !asset->name.isEmpty() ? asset->name : QFileInfo(clip.path).fileName();
        if (!asset || !asset->sequenceId.isEmpty()) {
            m_app->setLastMessage(tr("“%1” can't be transcribed: caption the clips inside it instead").arg(name),
                                  QStringLiteral("warning"));
            return false;
        }
        if (asset->hasAudioKnown && !asset->hasAudio) {
            m_app->setLastMessage(tr("“%1” has no sound").arg(name), QStringLiteral("warning"));
            return false;
        }
        if (seen.contains(clip.assetId))
            continue;
        seen.append(clip.assetId);
        const drift::TranscriptPtr stored = project.transcript(clip.assetId);
        const bool fresh = stored && stored->source.matches(asset->path)
                           && (lang.isEmpty() || stored->language.compare(lang, Qt::CaseInsensitive) == 0);
        if (fresh)
            continue;
        if (m_app->m_jobs->hasActive(QStringLiteral("transcribe"), clip.assetId)) {
            m_app->setLastMessage(tr("“%1” is already being transcribed").arg(name), QStringLiteral("warning"));
            return false;
        }
        work.append({clip.assetId, asset->path});
    }

    // Media transcribed already (here, or by an agent) captions without the model.
    if (!work.isEmpty() && !drift::WhisperTranscriber::modelPresent()) {
        m_app->setLastMessage(tr("Download speech recognition first"), QStringLiteral("warning"));
        return false;
    }

    m_runClipIds.clear();
    for (const drift::Clip &clip : std::as_const(clips))
        m_runClipIds.append(clip.id);
    m_runLanguage = lang;
    m_runWordsPerCue = std::max(0, maxWordsPerCue);
    m_runError.clear();
    m_runCancelled = false;

    // Everything already transcribed: the caption clip is built straight from the words.
    if (work.isEmpty()) {
        finishCaptions();
        return true;
    }

    m_jobIds.clear();
    m_jobCount = int(work.size());
    m_jobsDone = 0;
    setCaptionProgress(0.0);
    setCaptionStatus(tr("Getting speech recognition ready…"));
    setCaptioning(true);

    drift::LocalTranscribeOptions options;
    options.language = lang;
    options.align = true;
    options.vad = true;

    const QPointer<AiToolsController> self(this);
    for (int index = 0; index < work.size(); ++index) {
        const Work item = work.at(index);
        const drift::SourceFingerprint fingerprint = drift::SourceFingerprint::of(item.path);
        auto holder = std::make_shared<std::shared_ptr<drift::Transcript>>();
        // Called on the job's thread; the panel's bar is updated on ours.
        auto report = [self, index](double fraction, const QString &status) {
            if (!self)
                return;
            QMetaObject::invokeMethod(
                self.data(),
                [self, index, fraction, status] {
                    if (self)
                        self->reportJobProgress(index, fraction, status);
                },
                Qt::QueuedConnection);
        };
        const QString id = m_app->m_jobs->start(
            QStringLiteral("transcribe"), item.assetId, JobRegistry::Lane::Model,
            [path = item.path, options, fingerprint, holder, report](JobContext &ctx) {
                bool cancelled = false;
                const std::vector<float> pcm = drift::readMono16k(
                    path, 0, -1,
                    [&ctx, &report](double f) {
                        ctx.progress(0.05 * f, QStringLiteral("Reading audio…"));
                        report(0.05 * f, AiToolsController::tr("Reading the audio…"));
                        return !ctx.cancelled();
                    },
                    &cancelled);
                if (cancelled || ctx.cancelled())
                    return;
                if (pcm.empty())
                    return ctx.fail(QStringLiteral("no_audio"),
                                    AiToolsController::tr("No sound could be read from this file"));
                const drift::LocalTranscribeResult result = drift::transcribeLocal(
                    pcm, 0, options, [&ctx, &report](double f, const QString &status) {
                        ctx.progress(0.05 + 0.95 * f, status);
                        report(0.05 + 0.95 * f, status);
                        return !ctx.cancelled();
                    });
                if (result.cancelled || ctx.cancelled())
                    return;
                if (!result.transcript)
                    return ctx.fail(QStringLiteral("model_error"), result.error);
                result.transcript->source = fingerprint;
                *holder = result.transcript;
                ctx.succeed({{QStringLiteral("words"), result.transcript->speechTokenCount()},
                             {QStringLiteral("engine"), result.transcript->engine},
                             {QStringLiteral("language"), result.transcript->language},
                             {QStringLiteral("aligned"), result.transcript->wordTimingsAligned}});
            },
            [self, assetId = item.assetId, holder](const QJsonObject &job) {
                if (!self)
                    return QJsonObject{};
                const bool ok = job.value(QStringLiteral("ok")).toBool();
                if (ok && *holder)
                    self->storeTranscript(assetId, *holder);
                const QJsonObject error = job.value(QStringLiteral("error")).toObject();
                self->jobDone(job.value(QStringLiteral("id")).toString(), ok,
                              error.value(QStringLiteral("code")).toString(),
                              error.value(QStringLiteral("message")).toString());
                return QJsonObject{};
            });
        m_jobIds.append(id);
    }
    return true;
}

void AiToolsController::cancelCaptions()
{
    if (!m_captioning)
        return;
    setCaptionStatus(tr("Stopping…"));
    for (const QString &id : std::as_const(m_jobIds))
        m_app->m_jobs->cancel(id);
}

void AiToolsController::reportJobProgress(int index, double fraction, const QString &status)
{
    if (!m_captioning)
        return;
    // The model lane runs one job at a time, in the order they were queued.
    const double overall = (double(index) + qBound(0.0, fraction, 1.0)) / double(std::max(1, m_jobCount));
    setCaptionProgress(std::min(0.99, overall));
    if (status.isEmpty())
        return;
    setCaptionStatus(m_jobCount > 1 ? tr("%1 (file %2 of %3)").arg(status).arg(index + 1).arg(m_jobCount)
                                    : status);
}

void AiToolsController::jobDone(const QString &jobId, bool ok, const QString &code, const QString &message)
{
    if (!m_jobIds.removeOne(jobId))
        return;
    ++m_jobsDone;
    if (!ok) {
        if (code == QLatin1String("cancelled"))
            m_runCancelled = true;
        else if (m_runError.isEmpty())
            m_runError = message.isEmpty() ? tr("Speech recognition failed") : message;
        // The caption clip needs every transcript, so the rest would be wasted work.
        for (const QString &id : std::as_const(m_jobIds))
            m_app->m_jobs->cancel(id);
    }
    if (!m_jobIds.isEmpty())
        return;
    if (!m_runError.isEmpty())
        endCaptionRun(false, m_runError);
    else if (m_runCancelled)
        endCaptionRun(false, tr("Captions cancelled"));
    else
        finishCaptions();
}

void AiToolsController::finishCaptions()
{
    QList<QPair<int, int>> pairs;
    for (const QString &id : std::as_const(m_runClipIds)) {
        const QVariantList at = m_app->locateClip(id);
        if (at.size() == 2)
            pairs.append(qMakePair(at.at(0).toInt(), at.at(1).toInt()));
    }
    if (pairs.isEmpty()) {
        endCaptionRun(false, tr("The clips to caption are no longer on the timeline"));
        return;
    }
    setCaptionStatus(tr("Building the captions…"));
    // Every transcript is stored now, so this takes the word-exact path and returns at once.
    const bool ok = m_app->generateSubtitlesForClips(pairs, m_runLanguage, m_runWordsPerCue);
    if (ok)
        m_lastCaptionedClipId = m_runClipIds.first();
    endCaptionRun(ok, ok ? tr("Captions added") : tr("No speech was found to caption"));
}

void AiToolsController::endCaptionRun(bool ok, const QString &message)
{
    m_jobIds.clear();
    m_jobCount = 0;
    m_jobsDone = 0;
    setCaptionProgress(ok ? 1.0 : 0.0);
    setCaptionStatus(message);
    setCaptioning(false);
    if (!ok && !m_runCancelled && !m_runError.isEmpty())
        m_app->setLastMessage(m_runError, QStringLiteral("error"));
    emit captionsFinished(ok, message);
    scheduleRefresh();
}

void AiToolsController::storeTranscript(const QString &assetId, std::shared_ptr<drift::Transcript> transcript)
{
    drift::Project &project = m_app->m_project;
    const drift::MediaAsset *asset = project.asset(assetId);
    if (!asset || !transcript)
        return;
    // The file changed while it was being heard: these words belong to the old one.
    if (!transcript->source.isEmpty() && !transcript->source.matches(asset->path))
        return;
    project.setTranscript(assetId, std::move(transcript));
    if (m_app->m_mcpController)
        m_app->m_mcpController->bumpEditRevision();
    m_app->projectFile()->setDirty(true);
    scheduleRefresh();
}

bool AiToolsController::setLineText(int line, const QString &text)
{
    if (line < 0 || line >= m_lines.size() || m_targetAssetId.isEmpty())
        return false;
    drift::Project &project = m_app->m_project;
    const drift::TranscriptPtr current = project.transcript(m_targetAssetId);
    const Line ref = m_lines.at(line);
    // The words moved under this line since it was listed (an agent transcribed again, say).
    if (!current || speechText(*current, ref.firstWord, ref.lastWord) != ref.text) {
        refresh();
        return false;
    }

    auto edited = std::make_shared<drift::Transcript>(*current);
    const bool changed = drift::replaceTranscriptWords(*edited, ref.firstWord, ref.lastWord, text);
    if (changed) {
        project.setTranscript(m_targetAssetId, edited);
        if (m_app->m_mcpController)
            m_app->m_mcpController->bumpEditRevision();
        m_app->projectFile()->setDirty(true);
    }
    // Always, even when the words were already right: captions an undo put back catch up.
    const bool captions = syncCaptions(m_targetSourceKey, changed ? *edited : *current,
                                       ref.sourceStartUs, ref.sourceEndUs);
    refresh();
    return changed || captions;
}

bool AiToolsController::syncCaptions(const QString &sourceKey, const drift::Transcript &transcript,
                                     drift::TimeUs startUs, drift::TimeUs endUs)
{
    if (sourceKey.isEmpty())
        return false;
    drift::Project &project = m_app->m_project;
    const drift::Project before = project;

    // Every clip playing this media: a caption's words are read through the one it sits over.
    QList<drift::Clip> media;
    for (const drift::Track &track : project.tracks()) {
        for (const drift::Clip &clip : track.clips) {
            if (isMediaClip(clip) && drift::subtitleSourceKey(clip) == sourceKey)
                media.append(clip);
        }
    }
    if (media.isEmpty())
        return false;

    bool anyChanged = false;
    for (drift::Track &track : project.tracks()) {
        for (drift::Clip &sub : track.clips) {
            if (sub.type != drift::ClipType::Subtitle)
                continue;
            QList<drift::SubtitleCue> cues = sub.subtitleCues;
            bool changed = false;
            for (int k = int(cues.size()) - 1; k >= 0; --k) {
                const drift::SubtitleCue &cue = cues.at(k);
                if (cue.source != sourceKey)
                    continue;
                const drift::TimeUs a = sub.timelineStart + cue.startUs;
                const drift::TimeUs b = sub.timelineStart + cue.endUs;
                const drift::Clip *over = nullptr;
                drift::TimeUs best = 0;
                for (const drift::Clip &clip : std::as_const(media)) {
                    const drift::TimeUs overlap = std::min(b, clip.timelineEnd()) - std::max(a, clip.timelineStart);
                    if (overlap > best) {
                        best = overlap;
                        over = &clip;
                    }
                }
                if (!over)
                    continue;
                drift::TimeUs s0 = over->timelineToSourceUs(std::max(a, over->timelineStart));
                drift::TimeUs s1 = over->timelineToSourceUs(std::min(b, over->timelineEnd()));
                if (s1 < s0)
                    std::swap(s0, s1);
                if (s1 <= startUs || s0 >= endUs)
                    continue;
                const QString text = drift::transcriptTextInWindow(transcript, s0, s1);
                if (text == cue.text)
                    continue;
                if (text.isEmpty())
                    cues.removeAt(k);
                else
                    cues[k].text = text;
                changed = true;
            }
            if (!changed)
                continue;
            // A clip still carrying its generated name ("Subtitles (12)") keeps counting its cues.
            if (sub.name == drift::subtitleClipName(sub.subtitleCues))
                sub.name = drift::subtitleClipName(cues);
            sub.subtitleCues = cues;
            anyChanged = true;
        }
    }
    if (!anyChanged)
        return false;
    m_app->pushProjectEdit(before, tr("Caption corrected"));
    m_app->finishEdit(tr("Caption corrected"));
    return true;
}

void AiToolsController::seekToLine(int line)
{
    if (line < 0 || line >= m_lines.size())
        return;
    m_app->setPlayheadSeconds(drift::usToSeconds(m_lines.at(line).timelineStartUs));
}

bool AiToolsController::releaseSpeechModels()
{
    if (m_captioning || m_app->subtitleGenerating())
        return false;
    for (const QJsonValue &value : m_app->m_jobs->jobs()) {
        const QJsonObject job = value.toObject();
        const QString kind = job.value(QStringLiteral("kind")).toString();
        if (job.value(QStringLiteral("active")).toBool()
            && (kind == QLatin1String("transcribe") || kind == QLatin1String("diarize")))
            return false;
    }
    drift::WhisperTranscriber::instance().unload();
    drift::CtcAligner::instance().unload();
    drift::SpeakerDiarizer::instance().unload();
    return true;
}
