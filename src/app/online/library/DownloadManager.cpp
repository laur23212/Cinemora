#include "app/online/library/DownloadManager.h"

#include "app/online/library/LocalAssetDatabase.h"
#include "app/online/library/ProviderRegistry.h"
#include "app/online/library/SessionFiles.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

#include <memory>

namespace cadra::assets {

namespace {
constexpr int kMaxParallel = 3;
constexpr int kStallMs = 30000;
constexpr qint64 kMaxBytes = 4LL * 1024 * 1024 * 1024; // 4 GiB: refuse anything larger
} // namespace

// ─── session / library folders ───────────────────────────────────────────────────────────────

QString DownloadManager::sessionRoot() { return session::root(); }
bool DownloadManager::isSessionFile(const QString &path) { return session::isSessionFile(path); }
void DownloadManager::removeSession() { session::remove(); }
void DownloadManager::sweepStaleSessions() { session::sweepStale(); }

QString DownloadManager::libraryRoot()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/asset-library/files");
    QDir().mkpath(dir);
    return dir;
}

bool DownloadManager::looksLikeLottie(const QByteArray &head)
{
    if (head.startsWith("PK"))
        return true; // .lottie (zip); the bin importer unpacks it
    const QJsonObject o = QJsonDocument::fromJson(head).object();
    return o.contains(QStringLiteral("layers")) && (o.contains(QStringLiteral("v")) || o.contains(QStringLiteral("fr")));
}

// ─── manager ─────────────────────────────────────────────────────────────────────────────────

DownloadManager::DownloadManager(QNetworkAccessManager *network, ProviderRegistry *registry,
                                 LocalAssetDatabase *database, QObject *parent)
    : QObject(parent)
    , m_network(network)
    , m_registry(registry)
    , m_database(database)
{
    if (auto *app = QCoreApplication::instance()) {
        connect(app, &QCoreApplication::aboutToQuit, this, [this]() {
            for (const QString &key : m_jobs.keys())
                cancel(key);
            removeSession();
        });
    }
}

DownloadManager::~DownloadManager()
{
    for (Job &job : m_jobs)
        cleanupJob(job);
}

void DownloadManager::setKeepDownloads(bool keep)
{
    m_keep = keep;
}

void DownloadManager::bump()
{
    ++m_revision;
    emit revisionChanged();
}

DownloadManager::State DownloadManager::state(const QString &key) const
{
    const auto it = m_jobs.constFind(key);
    if (it != m_jobs.constEnd())
        return it->state;
    if (m_database) {
        if (const auto r = m_database->find(key); r && QFileInfo::exists(r->localPath))
            return State::Done;
    }
    return State::None;
}

double DownloadManager::progress(const QString &key) const
{
    return m_jobs.value(key).progress;
}

QString DownloadManager::message(const QString &key) const
{
    return m_jobs.value(key).message;
}

QString DownloadManager::targetPath(const AssetItem &item, const QString &suggestedName) const
{
    const QString root = m_keep ? libraryRoot() : sessionRoot();
    const QString dir = root + QLatin1Char('/') + util::safeFileName(item.providerId);
    QDir().mkpath(dir);
    QString name = suggestedName.isEmpty()
                       ? util::safeFileName(item.title) + QStringLiteral(" [") + util::safeFileName(item.id) + QStringLiteral("].")
                             + util::fileExtensionFor(item.downloadUrl, item.kind)
                       : suggestedName;
    // Never trust a server-provided name with path parts.
    name = QFileInfo(name).fileName();
    return dir + QLatin1Char('/') + name;
}

void DownloadManager::enqueue(const AssetItem &item)
{
    const QString key = item.key();
    if (m_jobs.contains(key) && m_jobs.value(key).state != State::Failed)
        return;
    // Downloaded before (this session, or kept): import again without refetching.
    if (m_database) {
        if (const auto r = m_database->find(key); r && QFileInfo::exists(r->localPath)) {
            const QStringList ids = m_importer ? m_importer({r->localPath}) : QStringList();
            AssetRecord updated = *r;
            updated.lastUsedAt = QDateTime::currentDateTimeUtc();
            m_database->upsert(updated);
            emit downloaded(key, ids.value(0), r->localPath);
            bump();
            return;
        }
    }
    Job job;
    job.item = item;
    m_jobs.insert(key, job);
    m_queue.enqueue(key);
    bump();
    pump();
}

void DownloadManager::pump()
{
    while (m_running < kMaxParallel && !m_queue.isEmpty()) {
        const QString key = m_queue.dequeue();
        if (!m_jobs.contains(key) || m_jobs.value(key).state != State::Queued)
            continue;
        ++m_running;
        start(key);
    }
}

void DownloadManager::start(const QString &key)
{
    Job &job = m_jobs[key];
    job.state = State::Resolving;
    job.attempts += 1;
    bump();
    AssetProvider *provider = m_registry ? m_registry->provider(job.item.providerId) : nullptr;
    if (!provider) {
        // Pasted links and catalog items without a provider: download the URL as is.
        DownloadSpec spec;
        spec.request = QNetworkRequest(job.item.downloadUrl);
        spec.request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        transfer(key, spec);
        return;
    }
    job.resolve = provider->download(job.item, [this, key](const DownloadSpec &spec, const ProviderError &error) {
        if (!m_jobs.contains(key))
            return;
        if (!error.ok()) {
            fail(key, error);
            return;
        }
        transfer(key, spec);
    });
}

void DownloadManager::transfer(const QString &key, const DownloadSpec &spec)
{
    Job &job = m_jobs[key];
    const QString scheme = spec.request.url().scheme();
    if (scheme != QLatin1String("https") && scheme != QLatin1String("http") && scheme != QLatin1String("file")
        && scheme != QLatin1String("qrc")) {
        fail(key, ProviderError::make(ProviderError::Code::Forbidden, tr("Unsupported download address.")));
        return;
    }
    const QString path = targetPath(job.item, spec.suggestedFileName);
    job.file = new QSaveFile(path);
    if (!job.file->open(QIODevice::WriteOnly)) {
        fail(key, ProviderError::make(ProviderError::Code::Storage, tr("Can't write to the download folder: %1").arg(job.file->errorString())));
        return;
    }
    job.state = State::Downloading;
    job.progress = 0;
    bump();

    QNetworkReply *reply = m_network->get(spec.request);
    job.reply = reply;
    job.stall = new QTimer(this);
    job.stall->setSingleShot(true);
    job.stall->setInterval(kStallMs);
    connect(job.stall, &QTimer::timeout, reply, [reply]() { reply->abort(); });
    job.stall->start();

    connect(reply, &QNetworkReply::readyRead, this, [this, key, reply]() {
        auto it = m_jobs.find(key);
        if (it == m_jobs.end() || !it->file)
            return;
        it->file->write(reply->readAll());
        if (it->file->size() > kMaxBytes)
            reply->abort();
        if (it->stall)
            it->stall->start();
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, key](qint64 got, qint64 total) {
        auto it = m_jobs.find(key);
        if (it == m_jobs.end() || total <= 0)
            return;
        const double p = double(got) / double(total);
        if (p - it->progress >= 0.01 || p >= 1.0) {
            it->progress = p;
            bump();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, key, reply, path]() {
        reply->deleteLater();
        auto it = m_jobs.find(key);
        if (it == m_jobs.end())
            return;
        if (it->stall) {
            it->stall->stop();
            it->stall->deleteLater();
            it->stall = nullptr;
        }
        if (reply->error() != QNetworkReply::NoError) {
            it->file->cancelWriting();
            delete it->file;
            it->file = nullptr;
            ProviderError error;
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (reply->error() == QNetworkReply::OperationCanceledError && it->state == State::None)
                error = ProviderError::make(ProviderError::Code::Cancelled, tr("Cancelled"));
            else if (reply->error() == QNetworkReply::OperationCanceledError)
                error = ProviderError::make(ProviderError::Code::Timeout, tr("The download stalled. Try again."));
            else if (status == 401 || status == 403)
                error = ProviderError::make(ProviderError::Code::AuthExpired, tr("The provider refused the download. Reconnect it in Settings."), status);
            else if (status == 429)
                error = ProviderError::make(ProviderError::Code::RateLimited, tr("Too many downloads at once. Try again in a minute."), status,
                                            util::retryAfterSeconds(reply));
            else if (status >= 500)
                error = ProviderError::make(ProviderError::Code::ServiceUnavailable, tr("The provider had a problem. Try again later."), status);
            else if (status == 0)
                error = ProviderError::make(ProviderError::Code::Offline, tr("Download interrupted. Check your connection."));
            else
                error = ProviderError::make(ProviderError::Code::Unknown, tr("Download failed (%1).").arg(status), status);
            fail(key, error);
            return;
        }
        it->file->write(reply->readAll());
        const qint64 bytes = it->file->size();
        if (!it->file->commit()) {
            delete it->file;
            it->file = nullptr;
            fail(key, ProviderError::make(ProviderError::Code::Storage, tr("Could not save the file. Is the disk full?")));
            return;
        }
        delete it->file;
        it->file = nullptr;
        complete(key, path, bytes);
    });
}

void DownloadManager::complete(const QString &key, const QString &path, qint64 bytes)
{
    Job &job = m_jobs[key];
    if (bytes <= 0) {
        QFile::remove(path);
        fail(key, ProviderError::make(ProviderError::Code::BadResponse, tr("The provider sent an empty file.")));
        return;
    }
    if (job.item.kind == AssetKind::Sticker || job.item.kind == AssetKind::Title) {
        QFile f(path);
        const bool lottie = f.open(QIODevice::ReadOnly) && looksLikeLottie(f.read(16 * 1024 * 1024));
        const bool image = QFileInfo(path).suffix().toLower() == QLatin1String("png")
                        || QFileInfo(path).suffix().toLower() == QLatin1String("gif")
                        || QFileInfo(path).suffix().toLower() == QLatin1String("webp");
        if (!lottie && !image) {
            f.close();
            QFile::remove(path);
            fail(key, ProviderError::make(ProviderError::Code::BadResponse,
                                          tr("That file is not a Lottie animation. For LottieFiles, copy the \"Lottie JSON\" link.")));
            return;
        }
    }

    if (m_database) {
        AssetRecord r;
        r.key = key;
        r.providerId = job.item.providerId;
        r.assetId = job.item.id;
        r.kind = job.item.kind;
        r.title = job.item.title;
        r.author = job.item.author;
        r.license = job.item.license;
        r.licenseUrl = job.item.licenseUrl.toString();
        r.attribution = job.item.attribution;
        r.pageUrl = job.item.pageUrl.toString();
        r.localPath = path;
        r.bytes = bytes;
        r.storage = isSessionFile(path) ? QStringLiteral("session") : QStringLiteral("library");
        r.downloadedAt = QDateTime::currentDateTimeUtc();
        r.lastUsedAt = r.downloadedAt;
        m_database->upsert(r);
    }
    const QStringList ids = m_importer ? m_importer({path}) : QStringList();
    job.state = State::Done;
    job.progress = 1.0;
    job.message.clear();
    m_jobs.remove(key); // state() now answers Done from the database
    --m_running;
    bump();
    if (m_importer && ids.isEmpty())
        emit failed(key, tr("Downloaded, but the media bin could not import it."));
    else
        emit downloaded(key, ids.value(0), path);
    pump();
}

void DownloadManager::fail(const QString &key, const ProviderError &error)
{
    auto it = m_jobs.find(key);
    if (it == m_jobs.end())
        return;
    const bool transient = error.code == ProviderError::Code::Offline || error.code == ProviderError::Code::Timeout
                        || error.code == ProviderError::Code::ServiceUnavailable;
    --m_running;
    if (transient && it->attempts < 2) {
        // One automatic retry after a short pause.
        it->state = State::Queued;
        it->message = tr("Retrying…");
        bump();
        QTimer::singleShot(2000, this, [this, key]() {
            if (m_jobs.contains(key) && m_jobs.value(key).state == State::Queued) {
                m_queue.enqueue(key);
                pump();
            }
        });
        return;
    }
    cleanupJob(*it);
    it->state = error.code == ProviderError::Code::Cancelled ? State::None : State::Failed;
    it->message = error.message;
    bump();
    if (error.code != ProviderError::Code::Cancelled)
        emit failed(key, error.message);
    if (it->state == State::None)
        m_jobs.erase(it);
    pump();
}

void DownloadManager::cleanupJob(Job &job)
{
    job.resolve.cancel();
    if (job.stall) {
        job.stall->stop();
        job.stall->deleteLater();
        job.stall = nullptr;
    }
    if (job.file) {
        job.file->cancelWriting();
        delete job.file;
        job.file = nullptr;
    }
}

void DownloadManager::cancel(const QString &key)
{
    auto it = m_jobs.find(key);
    if (it == m_jobs.end())
        return;
    if (it->state == State::Queued) {
        m_queue.removeAll(key);
        m_jobs.erase(it);
        bump();
        return;
    }
    it->state = State::None; // the finished handler reads this as a user cancel
    if (it->reply)
        it->reply->abort();
    else {
        cleanupJob(*it);
        --m_running;
        m_jobs.erase(it);
        bump();
        pump();
    }
}

// ─── Lottie previews ─────────────────────────────────────────────────────────────────────────

QString DownloadManager::previewPath(const QString &key) const
{
    const QString p = m_previews.value(key);
    return QFileInfo::exists(p) ? p : QString();
}

void DownloadManager::fetchPreview(const AssetItem &item)
{
    const QString key = item.key();
    if (!previewPath(key).isEmpty()) {
        emit previewReady(key, previewPath(key));
        return;
    }
    if (m_database) {
        if (const auto r = m_database->find(key); r && QFileInfo::exists(r->localPath)) {
            m_previews.insert(key, r->localPath);
            emit previewReady(key, r->localPath);
            return;
        }
    }
    const QUrl url = item.previewUrl.isValid() ? item.previewUrl : item.downloadUrl;
    if (!url.isValid() || m_previewReplies.value(key))
        return;
    // Bundled library files are read straight from the resources.
    if (url.scheme() == QLatin1String("qrc")) {
        const QString dest = sessionRoot() + QStringLiteral("/previews/") + util::safeFileName(key) + QStringLiteral(".json");
        QDir().mkpath(QFileInfo(dest).absolutePath());
        QFile::remove(dest);
        if (QFile::copy(QLatin1Char(':') + url.path(), dest)) {
            QFile::setPermissions(dest, QFile::ReadOwner | QFile::WriteOwner);
            m_previews.insert(key, dest);
            emit previewReady(key, dest);
        }
        return;
    }
    QNetworkRequest r(url);
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    r.setTransferTimeout(20000);
    QNetworkReply *reply = m_network->get(r);
    m_previewReplies.insert(key, reply);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 got, qint64) {
        if (got > 8 * 1024 * 1024)
            reply->abort(); // previews are small; anything bigger is not a sticker
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, key]() {
        reply->deleteLater();
        m_previewReplies.remove(key);
        if (reply->error() != QNetworkReply::NoError)
            return;
        const QByteArray bytes = reply->readAll();
        if (!looksLikeLottie(bytes))
            return;
        const QString dest = sessionRoot() + QStringLiteral("/previews/")
                           + QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex().left(16))
                           + (bytes.startsWith("PK") ? QStringLiteral(".lottie") : QStringLiteral(".json"));
        QDir().mkpath(QFileInfo(dest).absolutePath());
        QSaveFile f(dest);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(bytes);
            if (f.commit()) {
                m_previews.insert(key, dest);
                emit previewReady(key, dest);
            }
        }
    });
}

} // namespace cadra::assets
