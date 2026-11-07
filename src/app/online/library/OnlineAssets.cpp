#include "app/online/library/OnlineAssets.h"

#include "app/online/library/AssetSearchService.h"
#include "app/online/library/AuthManager.h"
#include "app/online/library/DownloadManager.h"
#include "app/online/library/LocalAssetDatabase.h"
#include "app/online/library/ProviderRegistry.h"
#include "app/online/library/SecretStore.h"
#include "app/online/library/ThumbnailCache.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace cadra::assets {

namespace {
const QStringList kNativeCatalogIds = {QStringLiteral("pexels-video"), QStringLiteral("pixabay-video"),
                                       QStringLiteral("freesound-sfx"), QStringLiteral("lottie-starter")};
QString appData() { return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation); }
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
    ensureUserCatalog();
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

void OnlineAssets::rememberLottieLink(const QString &url)
{
    ensureUserCatalog();
    QFile file(catalogPath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject())
        return; // never overwrite a catalog the user is in the middle of fixing
    QJsonObject root = doc.object();
    QJsonArray sources = root.value(QStringLiteral("sources")).toArray();
    int index = -1;
    for (int i = 0; i < sources.size(); ++i) {
        if (sources.at(i).toObject().value(QStringLiteral("id")).toString() == QLatin1String("my-lotties"))
            index = i;
    }
    if (index < 0) {
        sources.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("my-lotties")},
                                   {QStringLiteral("name"), QStringLiteral("My Lotties")},
                                   {QStringLiteral("kind"), QStringLiteral("lottie")},
                                   {QStringLiteral("type"), QStringLiteral("static")},
                                   {QStringLiteral("items"), QJsonArray()}});
        index = int(sources.size()) - 1;
    }
    QJsonObject mine = sources.at(index).toObject();
    QJsonArray items = mine.value(QStringLiteral("items")).toArray();
    for (const QJsonValue &v : std::as_const(items)) {
        if (v.toObject().value(QStringLiteral("url")).toString() == url)
            return;
    }
    const QString title = QFileInfo(QUrl(url).path()).completeBaseName();
    items.append(QJsonObject{{QStringLiteral("title"), title.isEmpty() ? QStringLiteral("Lottie") : title},
                             {QStringLiteral("url"), url}});
    mine.insert(QStringLiteral("items"), items);
    sources.replace(index, mine);
    root.insert(QStringLiteral("sources"), sources);
    QSaveFile out(catalogPath());
    if (out.open(QIODevice::WriteOnly)) {
        out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        out.commit();
    }
    reloadCatalog();
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
        rememberLottieLink(url.toString());
        item.providerId = QStringLiteral("catalog-my-lotties");
        item.kind = AssetKind::Sticker;
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

} // namespace cadra::assets
