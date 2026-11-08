#pragma once

#include "app/online/library/AssetTypes.h"

#include <QHash>
#include <QObject>
#include <QPair>
#include <QUrl>
#include <QVariantList>

#include <functional>
#include <memory>

class QNetworkAccessManager;

namespace dafin::assets {

class AssetSearchService;
class AuthManager;
class DownloadManager;
class LocalAssetDatabase;
class ProviderRegistry;
class ThumbnailCache;
class UserLibrary;

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
    // Bumped whenever the user's own library changes (added, saved, deleted).
    Q_PROPERTY(int userLibraryRevision READ userLibraryRevision NOTIFY userLibraryChanged)

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

    // The user's own files (My Lotties, My sounds, imported .mogrt titles). `collection` is
    // "lotties", "sounds" or "titles".
    int userLibraryRevision() const { return m_userRevision; }
    UserLibrary *userLibrary() const { return m_userLibrary.get(); }
    // Which collection a panel showing these kinds accepts dropped files into ("" for none).
    Q_INVOKABLE QString userCollectionForKinds(const QStringList &kinds) const;
    Q_INVOKABLE QString userCollectionProvider(const QString &collection) const;
    Q_INVOKABLE QString userCollectionName(const QString &collection) const;
    // "Lottie animations (*.json *.lottie)" for a file dialog.
    Q_INVOKABLE QString userCollectionFilter(const QString &collection) const;
    // Whether any of these dropped URLs (files or folders) can go into the collection.
    Q_INVOKABLE bool userCollectionAccepts(const QString &collection, const QVariantList &urls) const;
    // Copies the files in for this session. Returns {added, duplicates, rejected, message}; the
    // message is also sent as notice().
    Q_INVOKABLE QVariantMap importUserFiles(const QString &collection, const QVariantList &urls);
    Q_INVOKABLE bool saveUserItem(const QVariantMap &item);
    Q_INVOKABLE int saveAllUserItems(const QString &collection);
    Q_INVOKABLE bool deleteUserItem(const QVariantMap &item);
    Q_INVOKABLE int unsavedUserItems(const QString &collection) const;
    Q_INVOKABLE int userItemCount(const QString &collection) const;
    // Moving the library to Dafin on another computer: every saved Lottie, sound and title in one
    // .dafinlib file, and the import that adds such a file's contents as saved items. Both return
    // {ok, count, duplicates, skipped, unsaved, message}; the message is also sent as notice().
    Q_INVOKABLE QVariantMap exportUserLibrary(const QUrl &fileUrl);
    Q_INVOKABLE QVariantMap importUserLibrary(const QUrl &fileUrl);
    Q_INVOKABLE int savedUserItemCount() const;
    // "Dafin library (*.dafinlib)" for a file dialog.
    Q_INVOKABLE QString userLibraryArchiveFilter() const;

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
    void userLibraryChanged();

private:
    QVariantMap describe(const QString &id) const;
    void ensureUserCatalog();
    QStringList localFilesFrom(const QString &collection, const QVariantList &urls) const;

    std::unique_ptr<QNetworkAccessManager> m_network;
    std::unique_ptr<AuthManager> m_auth;
    // Before the registry: its providers keep a guarded pointer to the library.
    std::unique_ptr<UserLibrary> m_userLibrary;
    std::unique_ptr<ProviderRegistry> m_registry;
    std::unique_ptr<LocalAssetDatabase> m_database;
    std::unique_ptr<DownloadManager> m_downloads;
    std::unique_ptr<ThumbnailCache> m_thumbnails;
    std::unique_ptr<AssetSearchService> m_search;
    int m_userRevision = 0;
    // Pasted Lottie links being downloaded: download key → {link, title}. Each lands in My Lotties.
    QHash<QString, QPair<QUrl, QString>> m_pendingLottieLinks;
};

} // namespace dafin::assets
