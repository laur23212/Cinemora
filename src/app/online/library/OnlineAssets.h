#pragma once

#include "app/online/library/AssetTypes.h"

#include <QObject>
#include <QVariantList>

#include <functional>
#include <memory>

class QNetworkAccessManager;

namespace cadra::assets {

class AssetSearchService;
class AuthManager;
class DownloadManager;
class LocalAssetDatabase;
class ProviderRegistry;
class ThumbnailCache;

// The online asset system as QML sees it (singleton "OnlineAssets"). It wires the services
// together and exposes provider status, sign-in actions, downloads and previews. Search lives in
// AssetSearchModel (one per panel). Secrets never cross into QML.
class OnlineAssets : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList providers READ providers NOTIFY providersChanged)
    Q_PROPERTY(int downloadsRevision READ downloadsRevision NOTIFY downloadsRevisionChanged)
    Q_PROPERTY(bool keepDownloads READ keepDownloads WRITE setKeepDownloads NOTIFY keepDownloadsChanged)
    Q_PROPERTY(QString credentialStorage READ credentialStorage CONSTANT)
    Q_PROPERTY(bool credentialStorageEncrypted READ credentialStorageEncrypted CONSTANT)
    Q_PROPERTY(QString catalogPath READ catalogPath CONSTANT)

public:
    using Importer = std::function<QStringList(const QStringList &)>;

    explicit OnlineAssets(QObject *parent = nullptr);
    ~OnlineAssets() override;

    void setImporter(Importer importer);
    ThumbnailCache *thumbnails() const { return m_thumbnails.get(); }
    ProviderRegistry *registry() const { return m_registry.get(); }
    AuthManager *auth() const { return m_auth.get(); }
    DownloadManager *downloads() const { return m_downloads.get(); }
    LocalAssetDatabase *database() const { return m_database.get(); }

    static bool isSessionFile(const QString &path);

    QVariantList providers() const;
    int downloadsRevision() const;
    bool keepDownloads() const;
    void setKeepDownloads(bool keep);
    QString credentialStorage() const;
    bool credentialStorageEncrypted() const;
    QString catalogPath() const;

    Q_INVOKABLE QVariantList providersFor(const QString &kind) const;
    Q_INVOKABLE QVariantMap provider(const QString &id) const;

    // Settings > Asset Providers
    Q_INVOKABLE void connectProvider(const QString &id, const QVariantMap &fields);
    Q_INVOKABLE bool beginSignIn(const QString &id, const QVariantMap &fields);
    Q_INVOKABLE void completeSignIn(const QString &id, const QString &code);
    Q_INVOKABLE void reconnect(const QString &id);
    Q_INVOKABLE void logout(const QString &id);
    Q_INVOKABLE void openSignupPage(const QString &id);
    Q_INVOKABLE void openCatalogFile();
    Q_INVOKABLE void reloadCatalog();

    // Panels
    Q_INVOKABLE void download(const QVariantMap &item);
    Q_INVOKABLE void cancelDownload(const QString &key);
    Q_INVOKABLE QString downloadState(const QString &key) const;
    Q_INVOKABLE double downloadProgress(const QString &key) const;
    Q_INVOKABLE QString downloadMessage(const QString &key) const;
    Q_INVOKABLE bool importLink(const QString &url);
    Q_INVOKABLE void preparePreview(const QVariantMap &item);
    Q_INVOKABLE QString previewPath(const QString &key) const;
    Q_INVOKABLE void fetchDetails(const QVariantMap &item);
    Q_INVOKABLE void openExternal(const QString &url);

signals:
    void providersChanged();
    void downloadsRevisionChanged();
    void keepDownloadsChanged();
    void authFinished(const QString &id, bool ok, const QString &message);
    void downloaded(const QString &key, const QString &binAssetId);
    void downloadFailed(const QString &key, const QString &message);
    void previewReady(const QString &key, const QString &path);
    void detailsReady(const QVariantMap &item);
    void notice(const QString &message);

private:
    QVariantMap describe(const QString &id) const;
    void ensureUserCatalog();
    void rememberLottieLink(const QString &url);

    std::unique_ptr<QNetworkAccessManager> m_network;
    std::unique_ptr<AuthManager> m_auth;
    std::unique_ptr<ProviderRegistry> m_registry;
    std::unique_ptr<LocalAssetDatabase> m_database;
    std::unique_ptr<DownloadManager> m_downloads;
    std::unique_ptr<ThumbnailCache> m_thumbnails;
    std::unique_ptr<AssetSearchService> m_search;
};

} // namespace cadra::assets
