#pragma once

#include "app/online/library/AssetProvider.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQueue>

#include <functional>

class QLockFile;
class QNetworkAccessManager;
class QSaveFile;
class QTimer;

namespace dafin::assets {

class LocalAssetDatabase;
class ProviderRegistry;

// Fetches full assets: asks the provider what to download (download()), streams it to disk,
// checks it, records it in the local asset database and imports it into the media bin.
//
// Where files go: a per-session temporary folder that is deleted when Dafin quits (sessions left
// by a crash are swept a day later), or — with "Keep downloaded assets" on — a permanent library
// folder. Session files a saved project uses are embedded into the .drift file on save
// (ProjectFileController::captureSave asks isSessionFile()).
class DownloadManager : public QObject
{
    Q_OBJECT
public:
    using Importer = std::function<QStringList(const QStringList &paths)>;
    enum class State { None, Queued, Resolving, Downloading, Done, Failed };

    DownloadManager(QNetworkAccessManager *network, ProviderRegistry *registry, LocalAssetDatabase *database,
                    QObject *parent = nullptr);
    ~DownloadManager() override;

    void setImporter(Importer importer) { m_importer = std::move(importer); }
    void setKeepDownloads(bool keep);
    bool keepDownloads() const { return m_keep; }

    void enqueue(const AssetItem &item);
    void cancel(const QString &key);

    State state(const QString &key) const;
    double progress(const QString &key) const;
    QString message(const QString &key) const;
    // Dafin: the downloaded file of a finished item that is still on disk and still in Media,
    // empty otherwise. What a "ready" card drags onto the timeline.
    QString localPath(const QString &key) const;
    int revision() const { return m_revision; }
    // Dafin: re-announces every state (a record changed outside a download, e.g. removed from Media).
    void refresh() { bump(); }

    // Lottie preview: fetch the JSON (small) into the session folder; previewReady() answers.
    void fetchPreview(const AssetItem &item);
    QString previewPath(const QString &key) const;

    // Session folder management (static: also used by the project save path and tests).
    static QString sessionRoot();
    static QString libraryRoot();
    static bool isSessionFile(const QString &path);
    static void removeSession();
    static void sweepStaleSessions();

    static bool looksLikeLottie(const QByteArray &head);

signals:
    void revisionChanged();
    void downloaded(const QString &key, const QString &binAssetId, const QString &path);
    void failed(const QString &key, const QString &message);
    void previewReady(const QString &key, const QString &path);

private:
    struct Job
    {
        AssetItem item;
        State state = State::Queued;
        double progress = 0;
        QString message;
        int attempts = 0;
        RequestHandle resolve;
        QPointer<QNetworkReply> reply;
        QSaveFile *file = nullptr;
        QTimer *stall = nullptr;
    };

    void pump();
    void start(const QString &key);
    void transfer(const QString &key, const DownloadSpec &spec);
    void complete(const QString &key, const QString &path, qint64 bytes);
    void fail(const QString &key, const ProviderError &error);
    void cleanupJob(Job &job);
    void bump();
    QString targetPath(const AssetItem &item, const QString &suggestedName) const;

    QNetworkAccessManager *m_network;
    ProviderRegistry *m_registry;
    LocalAssetDatabase *m_database;
    Importer m_importer;
    bool m_keep = false;
    QHash<QString, Job> m_jobs;
    QQueue<QString> m_queue;
    int m_running = 0;
    int m_revision = 0;
    QHash<QString, QString> m_previews;
    QHash<QString, QPointer<QNetworkReply>> m_previewReplies;
};

} // namespace dafin::assets
