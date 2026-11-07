#include "app/online/library/ThumbnailCache.h"

#include "app/online/library/ProviderRegistry.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>
#include <QBuffer>

#include <algorithm>

namespace cadra::assets {

namespace {
constexpr int kMaxParallel = 6;
constexpr qint64 kMaxImageBytes = 8 * 1024 * 1024;
} // namespace

ThumbnailCache::ThumbnailCache(QNetworkAccessManager *network, ProviderRegistry *registry, QString directory,
                               QObject *parent)
    : QObject(parent)
    , m_network(network)
    , m_registry(registry)
    , m_dir(std::move(directory))
    , m_memory(48 * 1024) // cost in KiB of decoded images
{
    QDir().mkpath(m_dir);
}

QString ThumbnailCache::imageSource(const QString &providerId, const QUrl &url)
{
    if (!url.isValid() || url.isEmpty())
        return {};
    return QStringLiteral("image://assetthumb/") + providerId + QLatin1Char('/')
         + QString::fromLatin1(QUrl::toPercentEncoding(url.toString()));
}

QString ThumbnailCache::pathFor(const QUrl &url) const
{
    return m_dir + QLatin1Char('/')
         + QString::fromLatin1(QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Sha1).toHex())
         + QStringLiteral(".img");
}

int ThumbnailCache::ttlFor(const QString &providerId) const
{
    if (m_registry) {
        if (AssetProvider *p = m_registry->provider(providerId))
            return p->info().thumbnailCacheSeconds;
    }
    return 24 * 3600;
}

void ThumbnailCache::fetch(const QString &providerId, const QUrl &url, Callback callback)
{
    const QString key = url.toString();
    if (QImage *hit = m_memory.object(key)) {
        callback(*hit, QString());
        return;
    }
    // Local files (bundled library, qrc) never touch the cache folder.
    if (url.isLocalFile() || url.scheme() == QLatin1String("qrc")) {
        const QString local = url.isLocalFile() ? url.toLocalFile() : QLatin1Char(':') + url.path();
        QImage image(local);
        callback(image, image.isNull() ? tr("Preview unavailable") : QString());
        return;
    }
    const QString path = pathFor(url);
    const QFileInfo fi(path);
    if (fi.exists() && fi.lastModified().secsTo(QDateTime::currentDateTime()) < ttlFor(providerId)) {
        QImage image(path);
        if (!image.isNull()) {
            m_memory.insert(key, new QImage(image), int(image.sizeInBytes() / 1024) + 1);
            callback(image, QString());
            return;
        }
        QFile::remove(path);
    }
    auto &waiters = m_waiters[key];
    waiters.append(std::move(callback));
    if (waiters.size() > 1)
        return; // already queued or running
    m_queue.enqueue({providerId, url});
    startNext();
}

void ThumbnailCache::startNext()
{
    while (m_running < kMaxParallel && !m_queue.isEmpty()) {
        const Pending p = m_queue.dequeue();
        QNetworkRequest request(p.url);
        if (m_registry) {
            if (AssetProvider *provider = m_registry->provider(p.providerId)) {
                AssetItem item;
                item.providerId = p.providerId;
                item.thumbnailUrl = p.url;
                request = provider->getThumbnail(item);
            }
        }
        ++m_running;
        QNetworkReply *reply = m_network->get(request);
        const QString key = p.url.toString();
        const QString path = pathFor(p.url);
        connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 got, qint64) {
            if (got > kMaxImageBytes)
                reply->abort(); // not a thumbnail
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, key, path]() {
            reply->deleteLater();
            --m_running;
            QImage image;
            QString error;
            if (reply->error() == QNetworkReply::NoError) {
                const QByteArray bytes = reply->readAll();
                QBuffer buffer;
                buffer.setData(bytes);
                QImageReader reader(&buffer);
                reader.setAllocationLimit(64); // MiB: refuse decompression bombs
                image = reader.read();
                if (!image.isNull()) {
                    QSaveFile f(path);
                    if (f.open(QIODevice::WriteOnly)) {
                        f.write(bytes);
                        f.commit();
                    }
                } else {
                    error = tr("Preview unavailable");
                }
            } else {
                error = tr("Preview unavailable");
            }
            finish(key, image, error);
            startNext();
        });
    }
}

void ThumbnailCache::finish(const QString &key, const QImage &image, const QString &error)
{
    if (!image.isNull())
        m_memory.insert(key, new QImage(image), int(image.sizeInBytes() / 1024) + 1);
    const QList<Callback> waiters = m_waiters.take(key);
    for (const Callback &cb : waiters)
        cb(image, error);
}

void ThumbnailCache::prune(qint64 maxBytes)
{
    struct Entry { QString path; qint64 size; QDateTime modified; };
    QList<Entry> entries;
    qint64 total = 0;
    const QDateTime now = QDateTime::currentDateTime();
    QDirIterator it(m_dir, {QStringLiteral("*.img")}, QDir::Files);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        // Anything older than a week is stale for every provider.
        if (fi.lastModified().secsTo(now) > 7 * 24 * 3600) {
            QFile::remove(fi.absoluteFilePath());
            continue;
        }
        entries.append({fi.absoluteFilePath(), fi.size(), fi.lastModified()});
        total += fi.size();
    }
    if (total <= maxBytes)
        return;
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.modified < b.modified; });
    for (const Entry &e : entries) {
        if (total <= maxBytes)
            break;
        if (QFile::remove(e.path))
            total -= e.size;
    }
}

// ─── QML image provider ──────────────────────────────────────────────────────────────────────

namespace {
class ThumbResponse : public QQuickImageResponse
{
public:
    ThumbResponse(ThumbnailCache *cache, const QString &providerId, const QUrl &url, const QSize &requested)
        : m_requested(requested)
    {
        if (!cache) {
            m_error = QStringLiteral("unavailable");
            QMetaObject::invokeMethod(this, &QQuickImageResponse::finished, Qt::QueuedConnection);
            return;
        }
        // The cache and its network access live on the GUI thread.
        QPointer<ThumbResponse> self(this);
        QMetaObject::invokeMethod(cache, [cache, providerId, url, self]() {
            cache->fetch(providerId, url, [self](const QImage &image, const QString &error) {
                if (!self)
                    return;
                self->m_image = image;
                self->m_error = error;
                emit self->finished();
            });
        }, Qt::QueuedConnection);
    }

    QQuickTextureFactory *textureFactory() const override
    {
        QImage img = m_image;
        if (!img.isNull() && m_requested.isValid() && m_requested.width() > 0 && m_requested.height() > 0
            && (img.width() > m_requested.width() * 2 || img.height() > m_requested.height() * 2))
            img = img.scaled(m_requested * 2, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        return QQuickTextureFactory::textureFactoryForImage(img);
    }
    QString errorString() const override { return m_error; }

private:
    QSize m_requested;
    QImage m_image;
    QString m_error;
};
} // namespace

QQuickImageResponse *AssetThumbnailImageProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    const int slash = id.indexOf(QLatin1Char('/'));
    const QString providerId = slash > 0 ? id.left(slash) : QString();
    const QUrl url(QUrl::fromPercentEncoding(id.mid(slash + 1).toUtf8()));
    return new ThumbResponse(m_cache.data(), providerId, url, requestedSize);
}

} // namespace cadra::assets
