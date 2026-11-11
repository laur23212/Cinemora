#include "app/online/library/AssetSearchService.h"

#include "app/online/library/AuthManager.h"
#include "app/online/library/ProviderRegistry.h"
#include "app/online/library/ThumbnailCache.h"

#include <QSet>

#include <algorithm>

namespace dafin::assets {

// ─── service ─────────────────────────────────────────────────────────────────────────────────

AssetSearchService::AssetSearchService(ProviderRegistry *registry, AuthManager *auth, QObject *parent)
    : QObject(parent)
    , m_registry(registry)
    , m_auth(auth)
{
}

QString AssetSearchService::cacheKey(const QString &providerId, const SearchRequest &r) const
{
    return providerId + QLatin1Char('|') + kindToString(r.kind) + QLatin1Char('|') + r.query.trimmed().toLower()
         + QLatin1Char('|') + QString::number(r.page) + QLatin1Char('|') + QString::number(r.pageSize)
         + QLatin1Char('|') + r.category;
}

int AssetSearchService::blockedSeconds(const QString &providerId) const
{
    const QDateTime until = m_blockedUntil.value(providerId);
    if (!until.isValid())
        return 0;
    return qMax<qint64>(0, QDateTime::currentDateTimeUtc().secsTo(until));
}

void AssetSearchService::clearCache()
{
    m_cache.clear();
}

QList<AssetProvider *> AssetSearchService::usableProviders(AssetKind kind) const
{
    QList<AssetProvider *> out;
    for (AssetProvider *p : m_registry->providersFor(kind)) {
        const ProviderInfo i = p->info();
        const ConnectionState s = m_auth ? m_auth->state(i.id) : ConnectionState::NotRequired;
        if (i.requiresAccount && s != ConnectionState::Connected)
            continue;
        out.append(p);
    }
    return out;
}

QList<AssetProvider *> AssetSearchService::lockedProviders(AssetKind kind) const
{
    QList<AssetProvider *> out;
    for (AssetProvider *p : m_registry->providersFor(kind)) {
        const ProviderInfo i = p->info();
        const ConnectionState s = m_auth ? m_auth->state(i.id) : ConnectionState::NotRequired;
        if (i.requiresAccount && s != ConnectionState::Connected)
            out.append(p);
    }
    return out;
}

RequestHandle AssetSearchService::search(const QString &providerId, const SearchRequest &request, SearchCallback callback)
{
    AssetProvider *provider = m_registry->provider(providerId);
    if (!provider) {
        callback({}, ProviderError::make(ProviderError::Code::NotFound, tr("Unknown provider")));
        return {};
    }
    if (const int wait = blockedSeconds(providerId); wait > 0) {
        callback({}, ProviderError::make(ProviderError::Code::RateLimited,
                                         tr("%1 is limiting requests. Try again in %2 s.").arg(provider->info().name).arg(wait),
                                         429, wait));
        return {};
    }
    const QString key = cacheKey(providerId, request);
    const int ttl = provider->info().responseCacheSeconds;
    const auto hit = m_cache.constFind(key);
    if (hit != m_cache.constEnd() && hit->at.secsTo(QDateTime::currentDateTimeUtc()) < ttl) {
        callback(hit->page, ProviderError::none());
        return {};
    }
    return provider->search(request, [this, key, providerId, callback](const SearchPage &page, const ProviderError &error) {
        if (error.ok())
            m_cache.insert(key, {QDateTime::currentDateTimeUtc(), page});
        else if (error.code == ProviderError::Code::RateLimited)
            m_blockedUntil.insert(providerId, QDateTime::currentDateTimeUtc().addSecs(qMax(30, error.retryAfterSeconds)));
        callback(page, error);
    });
}

// ─── model ───────────────────────────────────────────────────────────────────────────────────

AssetSearchService *AssetSearchModel::s_service = nullptr;

void AssetSearchModel::setService(AssetSearchService *service)
{
    s_service = service;
}

AssetSearchModel::AssetSearchModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(300);
    connect(&m_debounce, &QTimer::timeout, this, &AssetSearchModel::refresh);
    if (s_service) {
        // A connect/logout in Settings changes which providers answer: search again.
        connect(s_service->auth(), &AuthManager::stateChanged, this, [this](const QString &) { scheduleRefresh(); });
        connect(s_service->registry(), &ProviderRegistry::providersChanged, this, &AssetSearchModel::scheduleRefresh);
        // The user's own library: a file added or deleted reloads the grid; a save only
        // updates its card, so the grid does not jump back to the top.
        connect(s_service->registry(), &ProviderRegistry::contentChanged, this,
                [this](const QString &) { scheduleRefresh(); });
        connect(s_service->registry(), &ProviderRegistry::itemChanged, this, &AssetSearchModel::updateItem);
    }
}

// A model lives as long as its tab (AssetsPanel loads one for the open tab only), so it often goes
// while a provider still waits on the network, e.g. the Dafin library on its index. Its requests
// are cancelled then: an answer that comes later is dropped instead of reaching a deleted model.
AssetSearchModel::~AssetSearchModel()
{
    cancelAll();
}

int AssetSearchModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QHash<int, QByteArray> AssetSearchModel::roleNames() const
{
    return {{ItemRole, "item"},           {KeyRole, "key"},           {TitleRole, "title"},
            {KindRole, "kind"},           {ThumbnailRole, "thumbnail"}, {DurationRole, "duration"},
            {WidthRole, "assetWidth"},    {HeightRole, "assetHeight"}, {ProviderNameRole, "providerName"},
            {DownloadableRole, "downloadable"}, {AuthorRole, "author"}, {LicenseRole, "license"}};
}

QVariant AssetSearchModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    const AssetItem &a = m_items.at(index.row());
    switch (role) {
    case ItemRole: return a.toVariant();
    case KeyRole: return a.key();
    case TitleRole: return a.title;
    case KindRole: return kindToString(a.kind);
    case ThumbnailRole: return ThumbnailCache::imageSource(a.providerId, a.thumbnailUrl);
    case DurationRole: return a.durationSeconds;
    case WidthRole: return a.width;
    case HeightRole: return a.height;
    case ProviderNameRole: {
        AssetProvider *p = s_service ? s_service->registry()->provider(a.providerId) : nullptr;
        return p ? p->info().name : a.providerId;
    }
    case DownloadableRole: return a.downloadable;
    case AuthorRole: return a.author;
    case LicenseRole: return a.license;
    default: return {};
    }
}

QVariantMap AssetSearchModel::get(int row) const
{
    return row >= 0 && row < m_items.size() ? m_items.at(row).toVariant() : QVariantMap();
}

void AssetSearchModel::updateItem(const QVariantMap &map)
{
    const AssetItem updated = AssetItem::fromVariant(map);
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).key() == updated.key()) {
            m_items[i] = updated;
            emit dataChanged(index(i), index(i));
            return;
        }
    }
}

void AssetSearchModel::setKinds(const QStringList &kinds)
{
    if (m_kinds == kinds)
        return;
    m_kinds = kinds;
    emit kindsChanged();
    scheduleRefresh();
}

void AssetSearchModel::setProviderId(const QString &id)
{
    if (m_providerId == id)
        return;
    m_providerId = id;
    emit providerIdChanged();
    scheduleRefresh();
}

void AssetSearchModel::setQuery(const QString &query)
{
    if (m_query == query)
        return;
    m_query = query;
    emit queryChanged();
    scheduleRefresh();
}

void AssetSearchModel::setCategory(const QString &category)
{
    if (m_category == category)
        return;
    m_category = category;
    emit categoryChanged();
    scheduleRefresh();
}

void AssetSearchModel::setPageSize(int size)
{
    size = qBound(6, size, 80);
    if (m_pageSize == size)
        return;
    m_pageSize = size;
    emit pageSizeChanged();
    scheduleRefresh();
}

void AssetSearchModel::scheduleRefresh()
{
    m_debounce.start();
}

void AssetSearchModel::cancelAll()
{
    for (RequestHandle &h : m_handles)
        h.cancel();
    m_handles.clear();
    m_roundBuffer.clear();
    m_pending = 0;
}

void AssetSearchModel::refresh()
{
    m_debounce.stop();
    cancelAll();
    ++m_generation;
    beginResetModel();
    m_items.clear();
    m_keys.clear();
    endResetModel();
    m_streams.clear();
    m_problems.clear();
    if (!s_service) {
        emit stateChanged();
        return;
    }
    for (const AssetKind kind : kindsFromStrings(m_kinds)) {
        for (AssetProvider *p : s_service->usableProviders(kind)) {
            if (m_providerId.isEmpty() || m_providerId == p->info().id)
                m_streams.append({p->info().id, kind, 1, false});
        }
        for (AssetProvider *p : s_service->lockedProviders(kind)) {
            if (!m_providerId.isEmpty() && m_providerId != p->info().id)
                continue;
            const ProviderInfo i = p->info();
            m_problems.append(QVariantMap{{QStringLiteral("providerId"), i.id},
                                          {QStringLiteral("name"), i.name},
                                          {QStringLiteral("code"), QStringLiteral("authRequired")},
                                          {QStringLiteral("message"), tr("Connect %1 for more results.").arg(i.name)}});
        }
    }
    startRound();
}

bool AssetSearchModel::hasMore() const
{
    for (const Stream &s : m_streams) {
        if (!s.exhausted)
            return true;
    }
    return false;
}

bool AssetSearchModel::canFetchMore(const QModelIndex &parent) const
{
    return !parent.isValid() && m_pending == 0 && hasMore();
}

void AssetSearchModel::fetchMore(const QModelIndex &parent)
{
    if (canFetchMore(parent))
        startRound();
}

void AssetSearchModel::startRound()
{
    m_roundBuffer.clear();
    const int generation = m_generation;
    m_starting = true;
    for (int i = 0; i < m_streams.size(); ++i) {
        Stream &s = m_streams[i];
        if (s.exhausted)
            continue;
        ++m_pending;
        SearchRequest r;
        r.query = m_query;
        r.kind = s.kind;
        r.page = s.nextPage;
        r.pageSize = m_pageSize;
        r.category = m_category;
        const QString providerId = s.providerId;
        // Besides the cancel in ~AssetSearchModel: a provider that answers a cancelled request
        // anyway still finds the model gone.
        const QPointer<AssetSearchModel> self(this);
        m_handles.append(s_service->search(providerId, r, [this, self, i, generation, providerId](
                                                                  const SearchPage &page, const ProviderError &error) {
            if (!self || generation != m_generation || i >= m_streams.size())
                return;
            Stream &st = m_streams[i];
            if (error.ok()) {
                st.nextPage += 1;
                st.exhausted = !page.hasMore || page.items.isEmpty();
                m_roundBuffer.insert(i, page.items);
            } else {
                st.exhausted = true;
                if (error.code != ProviderError::Code::Cancelled) {
                    AssetProvider *p = s_service->registry()->provider(providerId);
                    m_problems.append(QVariantMap{{QStringLiteral("providerId"), providerId},
                                                  {QStringLiteral("name"), p ? p->info().name : providerId},
                                                  {QStringLiteral("code"), error.codeName()},
                                                  {QStringLiteral("message"), error.message},
                                                  {QStringLiteral("retryAfter"), error.retryAfterSeconds}});
                }
            }
            if (--m_pending == 0 && !m_starting)
                finishRound();
        }));
    }
    m_starting = false;
    emit stateChanged();
    if (m_pending == 0)
        finishRound();
}

void AssetSearchModel::finishRound()
{
    m_handles.clear();
    // Interleave the round's pages so no single source floods the top of the grid. The user's
    // own files (My Lotties, My sounds, imported titles) are not mixed in: they lead the list.
    QList<AssetItem> merged;
    QList<int> order = m_roundBuffer.keys();
    std::sort(order.begin(), order.end());
    const auto isUserLibrary = [this](int stream) {
        if (stream < 0 || stream >= m_streams.size())
            return false;
        AssetProvider *p = s_service->registry()->provider(m_streams.at(stream).providerId);
        return p && p->info().userLibrary;
    };
    for (int stream : std::as_const(order)) {
        if (!isUserLibrary(stream))
            continue;
        for (const AssetItem &a : std::as_const(m_roundBuffer[stream])) {
            if (!m_keys.contains(a.key())) {
                m_keys.insert(a.key());
                merged.append(a);
            }
        }
    }
    order.erase(std::remove_if(order.begin(), order.end(), isUserLibrary), order.end());
    for (int pos = 0;; ++pos) {
        bool any = false;
        for (int stream : order) {
            const QList<AssetItem> &items = m_roundBuffer[stream];
            if (pos < items.size()) {
                any = true;
                const AssetItem &a = items.at(pos);
                if (!m_keys.contains(a.key())) {
                    m_keys.insert(a.key());
                    merged.append(a);
                }
            }
        }
        if (!any)
            break;
    }
    m_roundBuffer.clear();
    if (!merged.isEmpty()) {
        beginInsertRows({}, int(m_items.size()), int(m_items.size() + merged.size() - 1));
        m_items.append(merged);
        endInsertRows();
    }
    emit stateChanged();
}

QVariantList AssetSearchModel::sources() const
{
    QVariantList out;
    if (!s_service)
        return out;
    QSet<QString> seen;
    for (const AssetKind kind : kindsFromStrings(m_kinds)) {
        for (AssetProvider *p : s_service->registry()->providersFor(kind)) {
            const ProviderInfo i = p->info();
            if (seen.contains(i.id))
                continue;
            seen.insert(i.id);
            out.append(QVariantMap{{QStringLiteral("id"), i.id},
                                   {QStringLiteral("name"), i.name},
                                   {QStringLiteral("locked"), i.requiresAccount && s_service->auth()->state(i.id) != ConnectionState::Connected},
                                   {QStringLiteral("attributionText"), i.attributionText},
                                   {QStringLiteral("attributionUrl"), i.attributionUrl.toString()}});
        }
    }
    return out;
}

} // namespace dafin::assets
