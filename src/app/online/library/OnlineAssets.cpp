#include "app/online/library/OnlineAssets.h"

#include "app/online/library/AssetSearchService.h"
#include "app/online/library/AuthManager.h"
#include "app/online/library/DownloadManager.h"
#include "app/online/library/LocalAssetDatabase.h"
#include "app/online/library/ProviderRegistry.h"
#include "app/online/library/SecretStore.h"
#include "app/online/library/ThumbnailCache.h"
#include "app/online/library/UserLibrary.h"
#include "app/online/library/providers/UserLibraryProvider.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace dafin::assets {

namespace {
// "my-lotties" is the user's own library now (UserLibrary); its old catalog entries are migrated.
const QStringList kNativeCatalogIds = {QStringLiteral("pexels-video"), QStringLiteral("pixabay-video"),
                                       QStringLiteral("freesound-sfx"), QStringLiteral("lottie-starter"),
                                       QStringLiteral("my-lotties")};
QString appData() { return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation); }
const QString kLottieCollection = QStringLiteral("lotties");
} // namespace

OnlineAssets::OnlineAssets(QObject *parent)
    : QObject(parent)
    , m_network(std::make_unique<QNetworkAccessManager>())
{
    m_network->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    m_network->setStrictTransportSecurityEnabled(true);

    m_auth = std::make_unique<AuthManager>(SecretStore::createBest(appData() + QStringLiteral("/credentials")));
    m_registry = std::make_unique<ProviderRegistry>(m_network.get(), m_auth.get());
    m_auth->setRegistry(m_registry.get());
    m_registry->registerBuiltins();
    m_userLibrary = std::make_unique<UserLibrary>(appData() + QStringLiteral("/user-library"));
    for (const UserLibrary::Collection &c : UserLibrary::collections())
        m_registry->add(std::make_unique<UserLibraryProvider>(m_registry.get(), m_userLibrary.get(), c.id));
    ensureUserCatalog();
    m_userLibrary->migrateCatalogLinks(catalogPath());
    m_registry->loadCatalogProviders(catalogPath(), kNativeCatalogIds);
    m_auth->migrateLegacyKeys();
    m_auth->restore();

    m_database = std::make_unique<LocalAssetDatabase>(appData() + QStringLiteral("/asset-library/assets.json"));
    m_database->load();
    DownloadManager::sweepStaleSessions();
    m_database->reconcile();

    m_downloads = std::make_unique<DownloadManager>(m_network.get(), m_registry.get(), m_database.get());
    m_downloads->setKeepDownloads(QSettings().value(QStringLiteral("AssetProviders/keepDownloads"), false).toBool());

    m_thumbnails = std::make_unique<ThumbnailCache>(
        m_network.get(), m_registry.get(),
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/asset-thumbnails"));
    m_thumbnails->prune();

    m_search = std::make_unique<AssetSearchService>(m_registry.get(), m_auth.get());
    AssetSearchModel::setService(m_search.get());

    connect(m_auth.get(), &AuthManager::stateChanged, this, [this](const QString &) {
        m_search->clearCache();
        emit providersChanged();
    });
    connect(m_auth.get(), &AuthManager::authFinished, this, &OnlineAssets::authFinished);
    connect(m_registry.get(), &ProviderRegistry::providersChanged, this, [this]() {
        m_auth->restore();
        emit providersChanged();
    });
    connect(m_downloads.get(), &DownloadManager::revisionChanged, this, &OnlineAssets::downloadsRevisionChanged);
    connect(m_downloads.get(), &DownloadManager::downloaded, this,
            [this](const QString &key, const QString &binId, const QString &) { emit downloaded(key, binId); });
    connect(m_downloads.get(), &DownloadManager::failed, this, &OnlineAssets::downloadFailed);
    connect(m_downloads.get(), &DownloadManager::previewReady, this, &OnlineAssets::previewReady);

    // The user's own library: grids reload on add/delete and update one card on save.
    connect(m_userLibrary.get(), &UserLibrary::changed, this, [this](const QString &collection) {
        if (const UserLibrary::Collection *c = UserLibrary::collection(collection))
            m_registry->notifyContentChanged(c->providerId);
        ++m_userRevision;
        emit userLibraryChanged();
    });
    connect(m_userLibrary.get(), &UserLibrary::entryChanged, this, [this](const QString &collection, const QString &id) {
        const UserLibrary::Collection *c = UserLibrary::collection(collection);
        const std::optional<UserLibrary::Entry> e = m_userLibrary->entry(collection, id);
        if (c && e)
            m_registry->notifyItemChanged(UserLibraryProvider::toItem(*c, *e).toVariant());
        ++m_userRevision;
        emit userLibraryChanged();
    });
    // A pasted Lottie link, once fetched and checked, is kept in My Lotties.
    connect(m_downloads.get(), &DownloadManager::downloaded, this,
            [this](const QString &key, const QString &, const QString &path) {
                const auto it = m_pendingLottieLinks.constFind(key);
                if (it == m_pendingLottieLinks.constEnd())
                    return;
                const QPair<QUrl, QString> link = it.value();
                m_pendingLottieLinks.remove(key);
                m_userLibrary->addFetchedFile(kLottieCollection, path, link.second, link.first);
            });
    connect(m_downloads.get(), &DownloadManager::failed, this,
            [this](const QString &key, const QString &) { m_pendingLottieLinks.remove(key); });
}

OnlineAssets::~OnlineAssets()
{
    AssetSearchModel::setService(nullptr);
    if (m_database)
        m_database->flush();
}

void OnlineAssets::setImporter(Importer importer)
{
    m_downloads->setImporter(std::move(importer));
}

bool OnlineAssets::isSessionFile(const QString &path)
{
    return DownloadManager::isSessionFile(path);
}

QVariantMap OnlineAssets::describe(const QString &id) const
{
    AssetProvider *p = m_registry->provider(id);
    if (!p)
        return {};
    const ProviderInfo i = p->info();
    QVariantList kinds;
    for (AssetKind k : i.kinds)
        kinds.append(kindToString(k));
    QVariantList fields;
    for (const AuthField &f : i.fields)
        fields.append(QVariantMap{{QStringLiteral("id"), f.id}, {QStringLiteral("label"), f.label},
                                  {QStringLiteral("secret"), f.secret}, {QStringLiteral("optional"), f.optional},
                                  {QStringLiteral("placeholder"), f.placeholder}});
    const ConnectionState s = m_auth->state(id);
    return {
        {QStringLiteral("id"), i.id},
        {QStringLiteral("name"), i.name},
        {QStringLiteral("kinds"), kinds},
        {QStringLiteral("description"), i.description},
        {QStringLiteral("license"), i.license},
        {QStringLiteral("homepage"), i.homepage.toString()},
        {QStringLiteral("signupUrl"), i.signupUrl.toString()},
        {QStringLiteral("attributionText"), i.attributionText},
        {QStringLiteral("attributionUrl"), i.attributionUrl.toString()},
        {QStringLiteral("requiresAccount"), i.requiresAccount},
        {QStringLiteral("accountUnlocksMore"), i.accountUnlocksMore},
        {QStringLiteral("supportsOAuth"), i.supportsOAuth},
        {QStringLiteral("supportsRegistration"), i.supportsRegistration},
        {QStringLiteral("nonCommercialOnly"), i.nonCommercialOnly},
        {QStringLiteral("userLibrary"), i.userLibrary},
        {QStringLiteral("ownSidebarEntry"), i.ownSidebarEntry},
        {QStringLiteral("fields"), fields},
        {QStringLiteral("accountHint"), i.accountHint},
        {QStringLiteral("state"), connectionStateToString(s)},
        {QStringLiteral("stateMessage"), m_auth->message(id)},
        {QStringLiteral("busy"), m_auth->isBusy(id)},
        // Which fields hold a value — never the values themselves.
        {QStringLiteral("storedFields"), m_auth->storedFieldIds(id)},
        {QStringLiteral("signedIn"), m_auth->storedFieldIds(id).contains(QStringLiteral("refreshToken"))
                                         || m_auth->storedFieldIds(id).contains(QStringLiteral("accessToken"))},
    };
}

QVariantList OnlineAssets::providers() const
{
    QVariantList out;
    for (AssetProvider *p : m_registry->providers())
        out.append(describe(p->info().id));
    return out;
}

QVariantList OnlineAssets::providersFor(const QString &kind) const
{
    QVariantList out;
    for (AssetProvider *p : m_registry->providersFor(kindFromString(kind)))
        out.append(describe(p->info().id));
    return out;
}

QVariantMap OnlineAssets::provider(const QString &id) const
{
    return describe(id);
}

int OnlineAssets::downloadsRevision() const { return m_downloads->revision(); }
bool OnlineAssets::keepDownloads() const { return m_downloads->keepDownloads(); }

void OnlineAssets::setKeepDownloads(bool keep)
{
    if (keep == keepDownloads())
        return;
    m_downloads->setKeepDownloads(keep);
    QSettings().setValue(QStringLiteral("AssetProviders/keepDownloads"), keep);
    emit keepDownloadsChanged();
}

QString OnlineAssets::credentialStorage() const { return m_auth->storageName(); }
bool OnlineAssets::credentialStorageEncrypted() const { return m_auth->storageEncrypted(); }

void OnlineAssets::connectProvider(const QString &id, const QVariantMap &fields) { m_auth->connectProvider(id, fields); }

bool OnlineAssets::beginSignIn(const QString &id, const QVariantMap &fields)
{
    const QUrl url = m_auth->beginOAuth(id, fields);
    if (!url.isValid() || url.scheme() != QLatin1String("https"))
        return false;
    emit providersChanged();
    return QDesktopServices::openUrl(url);
}

void OnlineAssets::completeSignIn(const QString &id, const QString &code) { m_auth->completeOAuth(id, code); }
void OnlineAssets::reconnect(const QString &id) { m_auth->reconnect(id); }
void OnlineAssets::logout(const QString &id) { m_auth->logout(id); }

void OnlineAssets::openSignupPage(const QString &id)
{
    if (AssetProvider *p = m_registry->provider(id)) {
        const QUrl url = p->info().signupUrl;
        if (url.scheme() == QLatin1String("https"))
            QDesktopServices::openUrl(url);
    }
}

void OnlineAssets::openExternal(const QString &url)
{
    // Attribution links only; plain https.
    const QUrl u(url);
    if (u.scheme() == QLatin1String("https"))
        QDesktopServices::openUrl(u);
}

void OnlineAssets::download(const QVariantMap &item) { m_downloads->enqueue(AssetItem::fromVariant(item)); }
void OnlineAssets::cancelDownload(const QString &key) { m_downloads->cancel(key); }

QString OnlineAssets::downloadState(const QString &key) const
{
    switch (m_downloads->state(key)) {
    case DownloadManager::State::None: return QStringLiteral("none");
    case DownloadManager::State::Queued: return QStringLiteral("queued");
    case DownloadManager::State::Resolving: return QStringLiteral("queued");
    case DownloadManager::State::Downloading: return QStringLiteral("downloading");
    case DownloadManager::State::Done: return QStringLiteral("done");
    case DownloadManager::State::Failed: return QStringLiteral("failed");
    }
    return QStringLiteral("none");
}

double OnlineAssets::downloadProgress(const QString &key) const { return m_downloads->progress(key); }
QString OnlineAssets::downloadMessage(const QString &key) const { return m_downloads->message(key); }

void OnlineAssets::preparePreview(const QVariantMap &item) { m_downloads->fetchPreview(AssetItem::fromVariant(item)); }
QString OnlineAssets::previewPath(const QString &key) const { return m_downloads->previewPath(key); }

void OnlineAssets::fetchDetails(const QVariantMap &map)
{
    const AssetItem item = AssetItem::fromVariant(map);
    AssetProvider *p = m_registry->provider(item.providerId);
    if (!p)
        return;
    p->getAssetDetails(item, [this](const AssetItem &detailed, const ProviderError &) {
        emit detailsReady(detailed.toVariant());
    });
}

// ─── catalog (migrated from the Stock Media tab) ─────────────────────────────────────────────

QString OnlineAssets::catalogPath() const
{
    return appData() + QStringLiteral("/free-library/catalog.json");
}

void OnlineAssets::ensureUserCatalog()
{
    if (QFileInfo::exists(catalogPath()))
        return;
    QDir().mkpath(QFileInfo(catalogPath()).absolutePath());
    QFile::copy(QStringLiteral(":/free-library/catalog.json"), catalogPath());
    QFile::setPermissions(catalogPath(), QFile::ReadOwner | QFile::WriteOwner);
}

void OnlineAssets::openCatalogFile()
{
    ensureUserCatalog();
    QDesktopServices::openUrl(QUrl::fromLocalFile(catalogPath()));
}

void OnlineAssets::reloadCatalog()
{
    m_registry->loadCatalogProviders(catalogPath(), kNativeCatalogIds);
}

bool OnlineAssets::importLink(const QString &text)
{
    const QUrl url = QUrl::fromUserInput(text.trimmed());
    if (!url.isValid() || url.scheme() != QLatin1String("https")) {
        emit notice(tr("Paste a full link that starts with https://"));
        return false;
    }
    const QString path = url.path().toLower();
    const bool lottie = path.endsWith(QLatin1String(".json")) || path.endsWith(QLatin1String(".lottie"))
                     || url.host().endsWith(QLatin1String("lottie.host"));
    AssetItem item;
    item.downloadUrl = url;
    item.previewUrl = url;
    item.id = QString::fromLatin1(QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Sha1).toHex().left(12));
    item.title = QFileInfo(url.path()).completeBaseName();
    if (item.title.isEmpty())
        item.title = tr("Link");
    const QString suffix = QFileInfo(url.path()).suffix().toLower();
    if (lottie) {
        // Fetched like any sticker, then kept in My Lotties (saved) for next time.
        item.providerId = QStringLiteral("my-lotties");
        item.kind = AssetKind::Sticker;
        m_pendingLottieLinks.insert(item.key(), {url, item.title});
    } else {
        item.providerId = QStringLiteral("link");
        item.kind = QStringList{QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("ogg"), QStringLiteral("flac"),
                                QStringLiteral("m4a"), QStringLiteral("aac")}.contains(suffix)
                        ? AssetKind::SoundEffect
                        : AssetKind::Video;
    }
    m_downloads->enqueue(item);
    return true;
}

// ─── the user's own library (My Lotties, My sounds, imported titles) ─────────────────────────

QString OnlineAssets::userCollectionForKinds(const QStringList &kinds) const
{
    for (const QString &kind : kinds) {
        bool ok = false;
        const AssetKind k = kindFromString(kind, &ok);
        if (!ok)
            continue;
        if (k == AssetKind::Music)
            return QStringLiteral("sounds"); // the Audio panel: music and sound effects alike
        for (const UserLibrary::Collection &c : UserLibrary::collections()) {
            if (c.kind == k)
                return c.id;
        }
    }
    return {};
}

QString OnlineAssets::userCollectionProvider(const QString &collection) const
{
    const UserLibrary::Collection *c = UserLibrary::collection(collection);
    return c ? c->providerId : QString();
}

QString OnlineAssets::userCollectionName(const QString &collection) const
{
    return UserLibrary::displayName(collection);
}

QString OnlineAssets::userCollectionFilter(const QString &collection) const
{
    QStringList globs;
    for (const QString &suffix : UserLibrary::fileSuffixes(collection))
        globs.append(QStringLiteral("*.") + suffix);
    const QString label = collection == QLatin1String("lotties") ? tr("Lottie animations")
                        : collection == QLatin1String("sounds")  ? tr("Audio files")
                        : collection == QLatin1String("titles")  ? tr("Motion Graphics Templates")
                                                                 : tr("Files");
    return QStringLiteral("%1 (%2)").arg(label, globs.join(QLatin1Char(' ')));
}

QStringList OnlineAssets::localFilesFrom(const QString &collection, const QVariantList &urls) const
{
    QStringList files;
    for (const QVariant &v : urls) {
        const QUrl url = v.canConvert<QUrl>() && v.toUrl().isValid() ? v.toUrl() : QUrl(v.toString());
        const QString path = url.isLocalFile() ? url.toLocalFile() : QString();
        if (path.isEmpty())
            continue;
        const QFileInfo info(path);
        if (info.isDir()) {
            // A dropped folder brings every matching file inside it.
            QStringList globs;
            for (const QString &suffix : UserLibrary::fileSuffixes(collection))
                globs.append(QStringLiteral("*.") + suffix);
            QDirIterator it(path, globs, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext() && files.size() < 500)
                files.append(it.next());
        } else {
            files.append(path);
        }
    }
    return files;
}

bool OnlineAssets::userCollectionAccepts(const QString &collection, const QVariantList &urls) const
{
    if (!UserLibrary::collection(collection))
        return false;
    for (const QVariant &v : urls) {
        const QUrl url = v.canConvert<QUrl>() && v.toUrl().isValid() ? v.toUrl() : QUrl(v.toString());
        if (!url.isLocalFile())
            continue;
        const QFileInfo info(url.toLocalFile());
        if (info.isDir() || UserLibrary::fileSuffixes(collection).contains(info.suffix().toLower()))
            return true;
    }
    return false;
}

QVariantList OnlineAssets::urlsNotAcceptedBy(const QString &collection, const QVariantList &urls) const
{
    QVariantList rest;
    for (const QVariant &v : urls) {
        const QUrl url = v.canConvert<QUrl>() && v.toUrl().isValid() ? v.toUrl() : QUrl(v.toString());
        if (url.isLocalFile()) {
            const QFileInfo info(url.toLocalFile());
            // A folder was searched for the collection's files; its other media is left alone.
            if (info.isDir() || UserLibrary::fileSuffixes(collection).contains(info.suffix().toLower()))
                continue;
        }
        rest.append(url);
    }
    return rest;
}

QVariantMap OnlineAssets::importUserFiles(const QString &collection, const QVariantList &urls)
{
    if (!UserLibrary::collection(collection))
        return {};
    const UserLibrary::ImportResult r = m_userLibrary->importFiles(collection, localFilesFrom(collection, urls));
    const QString where = collection == QLatin1String("titles") ? tr("Titles") : UserLibrary::displayName(collection);
    QStringList parts;
    if (!r.added.isEmpty()) {
        parts.append(tr("Added %n file(s) to %1. Use Save to keep them after Dafin closes.", nullptr,
                        int(r.added.size())).arg(where));
    }
    if (r.duplicates > 0)
        parts.append(tr("%n file(s) already in %1.", nullptr, r.duplicates).arg(where));
    if (!r.error.isEmpty())
        parts.append(r.error);
    else if (!r.rejected.isEmpty()) {
        const QString kinds = collection == QLatin1String("lotties") ? tr("Lottie animations (.json, .lottie)")
                             : collection == QLatin1String("sounds")  ? tr("audio files")
                                                                      : tr("Motion Graphics Templates (.mogrt, or a .zip with one)");
        parts.append(tr("Skipped %n file(s): only %1 can go here.", nullptr, int(r.rejected.size())).arg(kinds));
    }
    const QString message = parts.join(QLatin1Char(' '));
    if (!message.isEmpty())
        emit notice(message);
    return {{QStringLiteral("added"), int(r.added.size())},
            {QStringLiteral("ids"), r.added},
            {QStringLiteral("duplicates"), r.duplicates},
            {QStringLiteral("rejected"), r.rejected},
            {QStringLiteral("message"), message}};
}

bool OnlineAssets::saveUserItem(const QVariantMap &map)
{
    const AssetItem item = AssetItem::fromVariant(map);
    const UserLibrary::Collection *c = UserLibrary::collectionForProvider(item.providerId);
    if (!c)
        return false;
    QString error;
    const bool ok = m_userLibrary->save(c->id, item.id, &error);
    if (!error.isEmpty())
        emit notice(error);
    return ok;
}

int OnlineAssets::saveAllUserItems(const QString &collection)
{
    if (!UserLibrary::collection(collection))
        return 0;
    const int saved = m_userLibrary->saveAll(collection);
    if (saved > 0)
        emit notice(tr("Saved %n file(s). They stay in Dafin until you delete them.", nullptr, saved));
    return saved;
}

bool OnlineAssets::deleteUserItem(const QVariantMap &map)
{
    const AssetItem item = AssetItem::fromVariant(map);
    const UserLibrary::Collection *c = UserLibrary::collectionForProvider(item.providerId);
    return c && m_userLibrary->remove(c->id, item.id);
}

int OnlineAssets::unsavedUserItems(const QString &collection) const
{
    return m_userLibrary->unsavedCount(collection);
}

int OnlineAssets::userItemCount(const QString &collection) const
{
    return int(m_userLibrary->entries(collection).size());
}

int OnlineAssets::savedUserItemCount() const
{
    return m_userLibrary->savedCount();
}

QString OnlineAssets::userLibraryArchiveFilter() const
{
    return tr("Dafin library (*.%1)").arg(UserLibrary::archiveSuffix());
}

QVariantMap OnlineAssets::exportUserLibrary(const QUrl &fileUrl)
{
    QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (path.isEmpty())
        return {};
    const QString suffix = QLatin1Char('.') + UserLibrary::archiveSuffix();
    if (!path.endsWith(suffix, Qt::CaseInsensitive))
        path += suffix;
    const UserLibrary::TransferResult r = m_userLibrary->exportArchive(path);
    QString message = r.error;
    if (r.error.isEmpty()) {
        message = tr("Exported %n saved item(s) to %1. In Dafin on the other computer, use Import library.", nullptr, r.count)
                      .arg(QFileInfo(path).fileName());
        if (r.unsaved > 0)
            message += QLatin1Char(' ')
                       + tr("%n unsaved file(s) were left out; save them and export again to include them.", nullptr, r.unsaved);
    }
    emit notice(message);
    return {{QStringLiteral("ok"), r.error.isEmpty()},
            {QStringLiteral("count"), r.count},
            {QStringLiteral("unsaved"), r.unsaved},
            {QStringLiteral("message"), message}};
}

QVariantMap OnlineAssets::importUserLibrary(const QUrl &fileUrl)
{
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (path.isEmpty())
        return {};
    const UserLibrary::TransferResult r = m_userLibrary->importArchive(path);
    QStringList parts;
    if (r.count > 0)
        parts.append(tr("Imported %n item(s) into My Lotties, My sounds and Titles.", nullptr, r.count));
    if (r.duplicates > 0)
        parts.append(tr("%n item(s) were already here.", nullptr, r.duplicates));
    if (r.skipped > 0 && r.error.isEmpty())
        parts.append(tr("%n item(s) couldn't be restored.", nullptr, r.skipped));
    if (!r.error.isEmpty())
        parts.append(r.error);
    const QString message = parts.join(QLatin1Char(' '));
    emit notice(message);
    return {{QStringLiteral("ok"), r.error.isEmpty()},
            {QStringLiteral("count"), r.count},
            {QStringLiteral("duplicates"), r.duplicates},
            {QStringLiteral("skipped"), r.skipped},
            {QStringLiteral("message"), message}};
}

} // namespace dafin::assets
