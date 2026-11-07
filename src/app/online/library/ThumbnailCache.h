#pragma once

#include <QCache>
#include <QHash>
#include <QImage>
#include <QNetworkRequest>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QQuickAsyncImageProvider>

#include <functional>

class QNetworkAccessManager;

namespace dafin::assets {

class ProviderRegistry;

// Disk + memory cache for result thumbnails. Files live in the cache directory keyed by a hash of
// the URL, expire per provider (Pixabay's 24 h rule by default) and are trimmed to a size cap,
// oldest first. Fetches are de-duplicated and limited to a few at a time; requests only happen
// for cards that are actually created by the grid (lazy loading).
class ThumbnailCache : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(const QImage &image, const QString &error)>;

    ThumbnailCache(QNetworkAccessManager *network, ProviderRegistry *registry, QString directory,
                   QObject *parent = nullptr);

    // GUI thread only.
    void fetch(const QString &providerId, const QUrl &url, Callback callback);

    // Removes expired files and trims the folder to `maxBytes`.
    void prune(qint64 maxBytes = 200 * 1024 * 1024);
    QString directory() const { return m_dir; }

    static QString imageSource(const QString &providerId, const QUrl &url);

private:
    struct Pending { QString providerId; QUrl url; };
    QString pathFor(const QUrl &url) const;
    int ttlFor(const QString &providerId) const;
    void startNext();
    void finish(const QString &key, const QImage &image, const QString &error);

    QNetworkAccessManager *m_network;
    ProviderRegistry *m_registry;
    QString m_dir;
    QCache<QString, QImage> m_memory;
    QHash<QString, QList<Callback>> m_waiters;
    QQueue<Pending> m_queue;
    int m_running = 0;
};

// image://assetthumb/<providerId>/<percent-encoded url>
class AssetThumbnailImageProvider : public QQuickAsyncImageProvider
{
public:
    explicit AssetThumbnailImageProvider(ThumbnailCache *cache) : m_cache(cache) {}
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

private:
    QPointer<ThumbnailCache> m_cache;
};

} // namespace dafin::assets
