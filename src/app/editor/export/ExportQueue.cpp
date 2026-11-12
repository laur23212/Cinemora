#include "app/editor/export/ExportQueue.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QUuid>

#ifdef Q_OS_WIN
#include <QProcess>
#endif

#if defined(Q_OS_LINUX)
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace {

QString statusKey(ExportQueue::Status status)
{
    switch (status) {
    case ExportQueue::Status::Queued:
        return QStringLiteral("queued");
    case ExportQueue::Status::Running:
        return QStringLiteral("running");
    case ExportQueue::Status::Done:
        return QStringLiteral("done");
    case ExportQueue::Status::Failed:
        return QStringLiteral("failed");
    case ExportQueue::Status::Cancelled:
        return QStringLiteral("cancelled");
    }
    return {};
}

bool samePath(const QString &a, const QString &b)
{
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    return QDir::cleanPath(a).compare(QDir::cleanPath(b), cs) == 0;
}

// The render thread gives way to the editor. QThreadPool's LowPriority covers Windows; on Linux
// a normal-policy thread's Qt priority does nothing, so the thread is niced instead — and the
// encoder's own threads, created from this one, inherit it.
void lowerThisThreadsPriority()
{
#if defined(Q_OS_LINUX)
    setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 10);
#endif
}

// A failed or cancelled render leaves no half-written file behind, unless the file was there
// before (the user chose to replace it, and it's already gone either way; keep what's left).
void removePartialOutput(const QString &path, bool existedBefore)
{
    if (!existedBefore && QFileInfo::exists(path))
        QFile::remove(path);
}

} // namespace

ExportQueue::ExportQueue(QObject *parent)
    : QAbstractListModel(parent)
{
    m_pool.setMaxThreadCount(1);
    m_pool.setThreadPriority(QThread::LowPriority);
    m_pool.setExpiryTimeout(-1);
}

ExportQueue::~ExportQueue()
{
    shutdown();
}

void ExportQueue::shutdown()
{
    m_blocked = true;
    for (Job &job : m_jobs) {
        if (job.status == Status::Running)
            job.cancel->storeRelaxed(1);
    }
    m_pool.waitForDone();
}

int ExportQueue::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_jobs.size());
}

QVariant ExportQueue::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_jobs.size())
        return {};
    const Job &job = m_jobs.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return job.snapshot.name;
    case JobIdRole:
        return job.id;
    case OutputPathRole:
        return job.snapshot.outputPath;
    case OutputNameRole:
        return QFileInfo(job.snapshot.outputPath).fileName();
    case FolderRole:
        return QFileInfo(job.snapshot.outputPath).absolutePath();
    case StatusRole:
        return statusKey(job.status);
    case ProgressRole:
        return job.progress;
    case StatusTextRole:
        return job.statusText;
    case UsedCpuFallbackRole:
        return job.usedCpuFallback;
    case SummaryRole:
        return summaryOf(job);
    default:
        return {};
    }
}

QHash<int, QByteArray> ExportQueue::roleNames() const
{
    return {
        {JobIdRole, "jobId"},
        {NameRole, "name"},
        {OutputPathRole, "outputPath"},
        {OutputNameRole, "outputName"},
        {FolderRole, "folder"},
        {StatusRole, "status"},
        {ProgressRole, "progress"},
        {StatusTextRole, "statusText"},
        {UsedCpuFallbackRole, "usedCpuFallback"},
        {SummaryRole, "summary"},
    };
}

int ExportQueue::pendingCount() const
{
    int n = 0;
    for (const Job &job : m_jobs) {
        if (job.status == Status::Queued || job.status == Status::Running)
            ++n;
    }
    return n;
}

double ExportQueue::currentProgress() const
{
    const Job *running = job(m_runningId);
    return running ? running->progress : 0.0;
}

int ExportQueue::indexOf(const QString &jobId) const
{
    for (int i = 0; i < m_jobs.size(); ++i) {
        if (m_jobs.at(i).id == jobId)
            return i;
    }
    return -1;
}

ExportQueue::Job *ExportQueue::job(const QString &jobId)
{
    const int i = indexOf(jobId);
    return i >= 0 ? &m_jobs[i] : nullptr;
}

const ExportQueue::Job *ExportQueue::job(const QString &jobId) const
{
    const int i = indexOf(jobId);
    return i >= 0 ? &m_jobs.at(i) : nullptr;
}

void ExportQueue::changed(const QString &jobId, const QList<int> &roles)
{
    const int i = indexOf(jobId);
    if (i < 0)
        return;
    const QModelIndex at = index(i);
    emit dataChanged(at, at, roles);
}

QString ExportQueue::summaryOf(const Job &job) const
{
    const QVariantMap &s = job.snapshot.renderSettings;
    if (s.value(QStringLiteral("gifExport")).toBool())
        return tr("GIF");
    if (s.value(QStringLiteral("audioOnly")).toBool())
        return tr("Audio · %1").arg(s.value(QStringLiteral("audioCodecId")).toString().toUpper());
    QString codec = s.value(QStringLiteral("videoCodecId")).toString();
    if (job.usedCpuFallback)
        codec = cpuFallbackCodec(codec);
    const int height = s.value(QStringLiteral("targetHeight")).toInt();
    const QString size = height > 0 ? tr("%1p").arg(height) : tr("Project size");
    return QStringLiteral("%1 · %2").arg(codec.toUpper().replace(QLatin1Char('_'), QLatin1Char(' ')), size);
}

QString ExportQueue::cpuFallbackCodec(const QString &codecId)
{
    static const char *const kHardwareSuffixes[] = {"_nvenc", "_qsv", "_amf", "_vaapi", "_videotoolbox",
                                                    "_mediacodec"};
    for (const char *suffix : kHardwareSuffixes) {
        const QLatin1StringView tail(suffix);
        if (!codecId.endsWith(tail))
            continue;
        const QString family = codecId.left(codecId.size() - tail.size());
        // The CPU AV1 encoder is SVT-AV1; H.264 and H.265 keep their plain ids (x264 / x265).
        if (family == QLatin1String("av1"))
            return QStringLiteral("av1_svt");
        return family;
    }
    return {};
}

QVariantMap ExportQueue::cpuFallbackSettings(const QVariantMap &settings)
{
    QVariantMap out = settings;
    const QString fallback = cpuFallbackCodec(settings.value(QStringLiteral("videoCodecId")).toString());
    if (fallback.isEmpty())
        return out;
    out.insert(QStringLiteral("videoCodecId"), fallback);
    // NVENC "p4", AMF "balanced"… mean nothing to x264; the runner uses the CPU encoder's default.
    out.remove(QStringLiteral("videoPreset"));
    return out;
}

void ExportQueue::openFolderOf(const QString &filePath)
{
    const QFileInfo info(filePath);
#ifdef Q_OS_WIN
    if (info.exists()) {
        QProcess::startDetached(QStringLiteral("explorer.exe"),
                                {QStringLiteral("/select,"), QDir::toNativeSeparators(info.absoluteFilePath())});
        return;
    }
#endif
    QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
}

void ExportQueue::openOutputFolder(const QString &jobId) const
{
    if (const Job *j = job(jobId))
        openFolderOf(j->snapshot.outputPath);
}

void ExportQueue::setBlocked(bool blocked)
{
    if (m_blocked == blocked)
        return;
    m_blocked = blocked;
    if (!m_blocked)
        QTimer::singleShot(0, this, &ExportQueue::startNext);
}

QString ExportQueue::enqueue(drift::RenderSnapshot snapshot)
{
    if (snapshot.outputPath.isEmpty()) {
        emit message(tr("That save location isn’t valid"), QStringLiteral("error"));
        return {};
    }
    for (const Job &other : std::as_const(m_jobs)) {
        if ((other.status == Status::Queued || other.status == Status::Running)
            && samePath(other.snapshot.outputPath, snapshot.outputPath)) {
            emit message(tr("Another export in the queue already writes %1")
                             .arg(QFileInfo(snapshot.outputPath).fileName()),
                         QStringLiteral("warning"));
            return {};
        }
    }

    Job job;
    job.id = snapshot.id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : snapshot.id;
    if (snapshot.name.isEmpty())
        snapshot.name = QFileInfo(snapshot.outputPath).completeBaseName();
    job.snapshot = std::move(snapshot);
    job.statusText = tr("Waiting");

    const int row = int(m_jobs.size());
    beginInsertRows({}, row, row);
    m_jobs.append(std::move(job));
    endInsertRows();
    emit countChanged();

    const Job &added = m_jobs.constLast();
    emit jobAdded(added.id, added.snapshot.name);
    QTimer::singleShot(0, this, &ExportQueue::startNext);
    return added.id;
}

void ExportQueue::addFromEditor(const QUrl &outputUrl, const QVariantMap &settings)
{
    if (!m_capture) {
        emit message(tr("The export queue isn’t available"), QStringLiteral("error"));
        return;
    }
    QPointer<ExportQueue> self(this);
    m_capture(
        outputUrl, settings,
        [self](drift::RenderSnapshot snapshot) {
            if (!self)
                return;
            const QStringList missing = snapshot.missingMedia();
            // Success is announced by jobAdded (its toast offers to show the queue).
            if (self->enqueue(std::move(snapshot)).isEmpty())
                return;
            if (!missing.isEmpty()) {
                emit self->message(tr("%n media file(s) of the queued export are missing and will render black.",
                                      nullptr, int(missing.size())),
                                   QStringLiteral("warning"));
            }
        },
        [self](const QString &error) {
            if (self)
                emit self->message(error, QStringLiteral("error"));
        });
}

void ExportQueue::cancel(const QString &jobId)
{
    Job *j = job(jobId);
    if (!j)
        return;
    if (j->status == Status::Queued) {
        j->status = Status::Cancelled;
        j->statusText = tr("Cancelled");
        changed(jobId, {StatusRole, StatusTextRole});
        emit countChanged();
    } else if (j->status == Status::Running) {
        j->cancel->storeRelaxed(1);
        j->statusText = tr("Cancelling…");
        changed(jobId, {StatusTextRole});
    }
}

void ExportQueue::remove(const QString &jobId)
{
    const int i = indexOf(jobId);
    if (i < 0)
        return;
    Job &j = m_jobs[i];
    if (j.status == Status::Running) {
        j.removeWhenStopped = true;
        cancel(jobId);
        return;
    }
    beginRemoveRows({}, i, i);
    m_jobs.removeAt(i);
    endRemoveRows();
    emit countChanged();
}

void ExportQueue::move(int from, int to)
{
    const int n = int(m_jobs.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to)
        return;
    // beginMoveRows names the row the item lands *before*, counted before the move.
    if (!beginMoveRows({}, from, from, {}, to > from ? to + 1 : to))
        return;
    m_jobs.move(from, to);
    endMoveRows();
}

void ExportQueue::clearFinished()
{
    for (int i = int(m_jobs.size()) - 1; i >= 0; --i) {
        const Status s = m_jobs.at(i).status;
        if (s == Status::Done || s == Status::Failed || s == Status::Cancelled) {
            beginRemoveRows({}, i, i);
            m_jobs.removeAt(i);
            endRemoveRows();
        }
    }
    emit countChanged();
}

void ExportQueue::startNext()
{
    if (busy() || m_blocked || !m_runner)
        return;
    Job *next = nullptr;
    for (Job &j : m_jobs) {
        if (j.status == Status::Queued) {
            next = &j;
            break;
        }
    }
    if (!next)
        return;

    next->status = Status::Running;
    next->progress = 0.0;
    next->statusText = tr("Rendering 0%");
    next->cancel->storeRelaxed(0);
    m_runningId = next->id;
    changed(next->id, {StatusRole, ProgressRole, StatusTextRole});
    emit busyChanged();
    emit currentProgressChanged();

    // The worker gets a copy that shares nothing with the model's, so the model may be read (or
    // the job removed) on this thread while the render reads its own.
    drift::RenderSnapshot snapshot = next->snapshot;
    snapshot.project = next->snapshot.project.detachedCopy();
    const QString jobId = next->id;
    const std::shared_ptr<QAtomicInt> cancelFlag = next->cancel;
    const Runner runner = m_runner;
    // The destructor waits for the pool, so `queue` outlives the worker; a reply posted while it is
    // being destroyed is dropped by Qt with it.
    ExportQueue *const queue = this;

    m_pool.start([queue, snapshot = std::move(snapshot), jobId, cancelFlag, runner]() {
        lowerThisThreadsPriority();

        const QString outputPath = snapshot.outputPath;
        const QVariantMap &settings = snapshot.renderSettings;
        const bool existedBefore = QFileInfo::exists(outputPath);
        const bool videoEncode = !settings.value(QStringLiteral("audioOnly")).toBool()
                                 && !settings.value(QStringLiteral("gifExport")).toBool();
        const QString fallbackCodec =
            videoEncode ? cpuFallbackCodec(settings.value(QStringLiteral("videoCodecId")).toString())
                        : QString();

        bool onFallback = false;
        double lastSent = -1.0;
        const ProgressFn progress = [&](double fraction) {
            // Every frame reports; the list only needs to move in visible steps.
            if (fraction >= 1.0 || fraction - lastSent >= 0.002) {
                lastSent = fraction;
                const bool fb = onFallback;
                QMetaObject::invokeMethod(
                    queue, [queue, jobId, fraction, fb]() { queue->onProgress(jobId, fraction, fb); },
                    Qt::QueuedConnection);
            }
            return cancelFlag->loadRelaxed() == 0;
        };

        QString error;
        bool ok = runner(snapshot, settings, outputPath, &error, progress);
        const bool cancelled = cancelFlag->loadRelaxed() != 0;
        if (!ok && !cancelled && !fallbackCodec.isEmpty()) {
            // The hardware encoder failed (driver, unsupported size, busy device…): same render on
            // the CPU encoder of the same format.
            qWarning("Export queue: hardware encoder failed (%s); retrying with %s",
                     qPrintable(error), qPrintable(fallbackCodec));
            removePartialOutput(outputPath, existedBefore);
            onFallback = true;
            lastSent = -1.0;
            progress(0.0);
            error.clear();
            ok = runner(snapshot, cpuFallbackSettings(settings), outputPath, &error, progress);
        }
        if (!ok)
            removePartialOutput(outputPath, existedBefore);

        QMetaObject::invokeMethod(
            queue, [queue, jobId, ok, error, onFallback]() { queue->onFinished(jobId, ok, error, onFallback); },
            Qt::QueuedConnection);
    });
}

void ExportQueue::onProgress(const QString &jobId, double fraction, bool fallback)
{
    Job *j = job(jobId);
    if (!j || j->status != Status::Running)
        return;
    j->progress = qBound(0.0, fraction, 1.0);
    j->usedCpuFallback = fallback;
    if (j->cancel->loadRelaxed() == 0) {
        const int percent = int(j->progress * 100.0);
        j->statusText = fallback ? tr("Rendering %1% · hardware encoder failed, using the CPU").arg(percent)
                                 : tr("Rendering %1%").arg(percent);
    }
    changed(jobId, {ProgressRole, StatusTextRole, UsedCpuFallbackRole, SummaryRole});
    if (jobId == m_runningId)
        emit currentProgressChanged();
}

void ExportQueue::onFinished(const QString &jobId, bool ok, const QString &error, bool fallback)
{
    if (m_runningId == jobId)
        m_runningId.clear();

    Job *j = job(jobId);
    if (j) {
        const bool cancelled = !ok && j->cancel->loadRelaxed() != 0;
        j->usedCpuFallback = fallback;
        j->progress = ok ? 1.0 : j->progress;
        if (ok) {
            j->status = Status::Done;
            j->statusText = fallback ? tr("Done · the hardware encoder failed, so the CPU encoder was used")
                                     : tr("Done");
        } else if (cancelled) {
            j->status = Status::Cancelled;
            j->statusText = tr("Cancelled");
        } else {
            j->status = Status::Failed;
            const QString reason = error.isEmpty() ? tr("Export failed") : error;
            j->statusText = fallback ? tr("Failed on the CPU encoder too: %1").arg(reason)
                                     : tr("Failed: %1").arg(reason);
        }
        changed(jobId);

        const QString name = j->snapshot.name;
        const QString outputPath = j->snapshot.outputPath;
        const QString statusText = j->statusText;
        const bool removeNow = j->removeWhenStopped;

        emit jobFinished(jobId, ok, cancelled, fallback, name, statusText, outputPath);
        if (!cancelled && m_notifier) {
            const QString file = QFileInfo(outputPath).fileName();
            const QString title = ok ? tr("Export finished") : tr("Export failed");
            QString body = ok ? tr("%1 is ready.").arg(file) : tr("%1: %2").arg(file, statusText);
            if (ok && fallback)
                body += QLatin1Char(' ') + tr("The hardware encoder failed, so the CPU encoder was used.");
            m_notifier(title, body, ok, [outputPath] { openFolderOf(outputPath); });
        }
        if (removeNow)
            remove(jobId);
        emit countChanged();
    }

    emit busyChanged();
    emit currentProgressChanged();
    QTimer::singleShot(0, this, &ExportQueue::startNext);
}
