#pragma once

#include <QAbstractListModel>
#include <QAtomicInt>
#include <QList>
#include <QString>
#include <QThreadPool>
#include <QUrl>
#include <QVariantMap>

#include <functional>
#include <memory>

#include "core/project/RenderSnapshot.h"

// Dafin: background export queue.
//
// Each job renders a drift::RenderSnapshot taken when it was added, so editing the project
// afterwards never changes a queued or running job. Jobs run one at a time, in list order, on a
// thread of their own at lower priority so the editor stays smooth. A job whose hardware encoder
// (NVENC, QSV, AMF, VAAPI…) fails is run again on the CPU encoder of the same format, and its
// status says so. Finishing or failing emits jobFinished (the in-app toast) and calls the
// notifier (the OS notification).
//
// The model knows nothing about the editor, the encoder or the OS: those come in as hooks
// (setCapture, setRunner, setNotifier), so the queue logic is testable on its own.
class ExportQueue : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY countChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double currentProgress READ currentProgress NOTIFY currentProgressChanged)

public:
    enum class Status { Queued, Running, Done, Failed, Cancelled };

    enum Roles {
        JobIdRole = Qt::UserRole + 1,
        NameRole,
        OutputPathRole,
        OutputNameRole,
        FolderRole,
        StatusRole,     // "queued" | "running" | "done" | "failed" | "cancelled"
        ProgressRole,   // 0..1
        StatusTextRole, // human readable, includes the CPU fallback note
        UsedCpuFallbackRole,
        SummaryRole,    // "H.264 · 1080p" style line
    };

    using ProgressFn = std::function<bool(double)>;
    // Renders `snapshot` with `settings` to `outputPath`. Runs on the queue's worker thread.
    using Runner = std::function<bool(const drift::RenderSnapshot &snapshot, const QVariantMap &settings,
                                      const QString &outputPath, QString *errorOut,
                                      const ProgressFn &progress)>;
    // Takes the editor's snapshot (after any pre-render step, e.g. template titles). Main thread.
    using Capture = std::function<void(const QUrl &outputUrl, const QVariantMap &settings,
                                       std::function<void(drift::RenderSnapshot)> done,
                                       std::function<void(const QString &)> failed)>;
    // OS notification. `openFolder` opens the job's output folder when the user picks the action.
    using Notifier = std::function<void(const QString &title, const QString &body, bool ok,
                                        std::function<void()> openFolder)>;

    explicit ExportQueue(QObject *parent = nullptr);
    ~ExportQueue() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_jobs.size()); }
    int pendingCount() const;
    bool busy() const { return m_runningId.size() > 0; }
    double currentProgress() const;

    void setRunner(Runner runner) { m_runner = std::move(runner); }
    void setCapture(Capture capture) { m_capture = std::move(capture); }
    void setNotifier(Notifier notifier) { m_notifier = std::move(notifier); }

    // While blocked (a normal export is running) no new job starts; the running one carries on.
    void setBlocked(bool blocked);
    bool blocked() const { return m_blocked; }

    // Stops the running job and waits for it; queued jobs stay queued. For application quit.
    void shutdown();

    // Adds a job that renders `snapshot` to snapshot.outputPath with snapshot.renderSettings.
    // Returns the job id, or an empty string when refused (message() says why).
    QString enqueue(drift::RenderSnapshot snapshot);

    // Snapshots the open project through the capture hook and queues it.
    Q_INVOKABLE void addFromEditor(const QUrl &outputUrl, const QVariantMap &settings);
    // Queued: marked cancelled. Running: stopped at the next frame.
    Q_INVOKABLE void cancel(const QString &jobId);
    // A running job is cancelled first and leaves the list when it has stopped.
    Q_INVOKABLE void remove(const QString &jobId);
    Q_INVOKABLE void move(int from, int to);
    Q_INVOKABLE void clearFinished();
    Q_INVOKABLE void openOutputFolder(const QString &jobId) const;
    Q_INVOKABLE void requestShowPanel() { emit showPanelRequested(); }
    Q_INVOKABLE int indexOf(const QString &jobId) const;

    // The CPU encoder with the same format as a hardware one ("h264_nvenc" → "h264"), or an empty
    // string when `codecId` is not a hardware encoder.
    static QString cpuFallbackCodec(const QString &codecId);
    // `settings` with the video encoder swapped for its CPU fallback and the hardware-only
    // preset dropped (the runner picks the CPU encoder's default).
    static QVariantMap cpuFallbackSettings(const QVariantMap &settings);
    // Opens the folder holding `filePath`, with the file selected where the OS allows it.
    static void openFolderOf(const QString &filePath);

signals:
    void countChanged();
    void busyChanged();
    void currentProgressChanged();
    void jobAdded(const QString &jobId, const QString &name);
    // ok = rendered; `message` is the status line; `outputPath` is the file.
    void jobFinished(const QString &jobId, bool ok, bool cancelled, bool usedCpuFallback,
                     const QString &name, const QString &message, const QString &outputPath);
    void message(const QString &text, const QString &severity);
    void showPanelRequested();

private:
    struct Job
    {
        QString id;
        drift::RenderSnapshot snapshot;
        Status status = Status::Queued;
        double progress = 0.0;
        QString statusText;
        QString error;
        bool usedCpuFallback = false;
        bool removeWhenStopped = false;
        std::shared_ptr<QAtomicInt> cancel = std::make_shared<QAtomicInt>(0);
    };

    Job *job(const QString &jobId);
    const Job *job(const QString &jobId) const;
    void changed(const QString &jobId, const QList<int> &roles = {});
    void startNext();
    void onProgress(const QString &jobId, double fraction, bool fallback);
    void onFinished(const QString &jobId, bool ok, const QString &error, bool fallback);
    QString summaryOf(const Job &job) const;

    QList<Job> m_jobs;
    QString m_runningId;
    bool m_blocked = false;
    Runner m_runner;
    Capture m_capture;
    Notifier m_notifier;
    QThreadPool m_pool;
};
