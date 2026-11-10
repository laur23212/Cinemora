#pragma once

#include "app/online/library/AssetProvider.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>

namespace dafin::assets {

class AuthManager;
class ProviderRegistry;

// Front door for searches: response cache (per provider TTL; Pixabay requires 24 h), a
// rate-limit gate (a provider that answered 429 is skipped until its Retry-After passes) and
// one place that knows which providers are usable right now.
class AssetSearchService : public QObject
{
    Q_OBJECT
public:
    AssetSearchService(ProviderRegistry *registry, AuthManager *auth, QObject *parent = nullptr);

    RequestHandle search(const QString &providerId, const SearchRequest &request, SearchCallback callback);

    // Providers that can answer for `kind` now (account present when required, not blocked).
    QList<AssetProvider *> usableProviders(AssetKind kind) const;
    // Providers for `kind` that need an account the user has not connected.
    QList<AssetProvider *> lockedProviders(AssetKind kind) const;
    int blockedSeconds(const QString &providerId) const;
    void clearCache();

    ProviderRegistry *registry() const { return m_registry; }
    AuthManager *auth() const { return m_auth; }

private:
    struct Cached { QDateTime at; SearchPage page; };
    QString cacheKey(const QString &providerId, const SearchRequest &r) const;

    ProviderRegistry *m_registry;
    AuthManager *m_auth;
    QHash<QString, Cached> m_cache;
    QHash<QString, QDateTime> m_blockedUntil;
};

// The list behind one panel's grid. Set `kinds` (and optionally `providerId`, `query`); results
// load one "round" at a time — page N from every provider, interleaved — and fetchMore() asks for
// the next round when the view nears the end. Thumbnails load lazily through image://assetthumb.
class AssetSearchModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QStringList kinds READ kinds WRITE setKinds NOTIFY kindsChanged)
    Q_PROPERTY(QString providerId READ providerId WRITE setProviderId NOTIFY providerIdChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(int pageSize READ pageSize WRITE setPageSize NOTIFY pageSizeChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY stateChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY stateChanged)
    // [{providerId, name, code, message, retryAfter}] for providers that failed this round, plus
    // providers that need an account (code "authRequired") — the panel shows them as hints.
    Q_PROPERTY(QVariantList problems READ problems NOTIFY stateChanged)
    Q_PROPERTY(QVariantList sources READ sources NOTIFY stateChanged)

public:
    enum Role { ItemRole = Qt::UserRole + 1, KeyRole, TitleRole, KindRole, ThumbnailRole, DurationRole,
                WidthRole, HeightRole, ProviderNameRole, DownloadableRole, AuthorRole, LicenseRole };

    explicit AssetSearchModel(QObject *parent = nullptr);
    ~AssetSearchModel() override;

    // Set once by the app before QML creates any model.
    static void setService(AssetSearchService *service);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;

    QStringList kinds() const { return m_kinds; }
    void setKinds(const QStringList &kinds);
    QString providerId() const { return m_providerId; }
    void setProviderId(const QString &id);
    QString query() const { return m_query; }
    void setQuery(const QString &query);
    int pageSize() const { return m_pageSize; }
    void setPageSize(int size);
    bool loading() const { return m_pending > 0; }
    bool hasMore() const;
    QVariantList problems() const { return m_problems; }
    QVariantList sources() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void loadMore() { fetchMore({}); }
    Q_INVOKABLE QVariantMap get(int row) const;
    // Replace one item (e.g. after getAssetDetails() added waveform peaks).
    Q_INVOKABLE void updateItem(const QVariantMap &item);

signals:
    void kindsChanged();
    void providerIdChanged();
    void queryChanged();
    void pageSizeChanged();
    void stateChanged();

private:
    struct Stream
    {
        QString providerId;
        AssetKind kind;
        int nextPage = 1;
        bool exhausted = false;
    };
    void scheduleRefresh();
    void startRound();
    void finishRound();
    void cancelAll();

    static AssetSearchService *s_service;
    QStringList m_kinds;
    QString m_providerId;
    QString m_query;
    int m_pageSize = 30;
    QList<AssetItem> m_items;
    QSet<QString> m_keys;
    QList<Stream> m_streams;
    QHash<int, QList<AssetItem>> m_roundBuffer; // stream index → items for the round in flight
    QList<RequestHandle> m_handles;
    QVariantList m_problems;
    int m_pending = 0;
    bool m_starting = false; // callbacks that answer synchronously wait for the loop
    int m_generation = 0;
    QTimer m_debounce;
};

} // namespace dafin::assets
