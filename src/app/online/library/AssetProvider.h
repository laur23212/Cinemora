#pragma once

// The contract every online asset source implements. Adding a provider = one subclass plus one
// line in ProviderRegistry::registerBuiltins(); nothing else in the app changes.
//
// All calls are asynchronous and run on the GUI thread (QNetworkAccessManager lives there).
// Each returns a RequestHandle the caller may cancel; a cancelled request never calls back.

#include "app/online/library/AssetTypes.h"

#include <QJsonDocument>
#include <QObject>
#include <QPointer>

#include <functional>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace cadra::assets {

// Cancellation token for an in-flight provider call.
class RequestHandle
{
public:
    RequestHandle() : m_cancelled(std::make_shared<bool>(false)) {}
    void attach(QNetworkReply *reply);
    void cancel();
    bool isCancelled() const { return *m_cancelled; }
    std::shared_ptr<bool> flag() const { return m_cancelled; }

private:
    std::shared_ptr<bool> m_cancelled;
    QPointer<QNetworkReply> m_reply;
};

// What a provider may use from the app. Owned by the registry, outlives every provider call.
class ProviderContext
{
public:
    virtual ~ProviderContext() = default;
    virtual QNetworkAccessManager *network() = 0;
    // The provider's stored credentials (empty map when none). Read fresh on every call so a
    // reconnect or logout applies immediately.
    virtual Credentials credentials(const QString &providerId) const = 0;
    // Providers that refresh tokens (OAuth) write the new ones back through this.
    virtual void updateCredentials(const QString &providerId, const Credentials &credentials) = 0;
    // Called when a provider sees its credentials rejected; the auth manager marks it Expired.
    virtual void reportAuthFailure(const QString &providerId, const ProviderError &error) = 0;
    virtual QByteArray userAgent() const = 0;
};

using SearchCallback = std::function<void(const SearchPage &page, const ProviderError &error)>;
using DetailsCallback = std::function<void(const AssetItem &item, const ProviderError &error)>;
using DownloadSpecCallback = std::function<void(const DownloadSpec &spec, const ProviderError &error)>;
using AuthCallback = std::function<void(const Credentials &credentials, const ProviderError &error)>;

class AssetProvider
{
public:
    explicit AssetProvider(ProviderContext *context) : m_context(context) {}
    virtual ~AssetProvider() = default;

    virtual ProviderInfo info() const = 0;

    // Validate/obtain credentials from what the user typed (`input` = AuthField id → value) or,
    // for OAuth, from the authorization code (`input["code"]`). On success the returned
    // credentials are what the auth manager stores. Default: accept the fields as typed after a
    // cheap test search, so a wrong key is reported at connect time, not on the first browse.
    virtual RequestHandle authenticate(const QVariantMap &input, AuthCallback callback);

    // Drop any runtime session state (cached tokens). Stored secrets are removed by the auth
    // manager; providers with a server-side revoke endpoint call it here.
    virtual void logout() {}

    virtual RequestHandle search(const SearchRequest &request, SearchCallback callback) = 0;

    // Richer metadata for one item (licence detail, all renditions). Default: the item as is.
    virtual RequestHandle getAssetDetails(const AssetItem &item, DetailsCallback callback);

    // Resolve what to fetch for the full asset (best rendition, auth header) or refuse.
    virtual RequestHandle download(const AssetItem &item, DownloadSpecCallback callback);

    // Request for the item's thumbnail (headers if the CDN needs them). Default: thumbnailUrl.
    virtual QNetworkRequest getThumbnail(const AssetItem &item) const;

    // URL a browser opens for interactive sign-in (OAuth). Empty when not supported.
    virtual QUrl oauthAuthorizationUrl(const Credentials &partial) const { Q_UNUSED(partial); return {}; }

protected:
    ProviderContext *context() const { return m_context; }
    Credentials credentials() const { return m_context->credentials(info().id); }
    QNetworkRequest makeRequest(const QUrl &url) const;

    // GET + JSON parse + error mapping, the common case for search APIs. `parse` turns the
    // document into a page; it never runs for a cancelled request.
    RequestHandle getJson(const QNetworkRequest &request,
                          std::function<void(const QJsonDocument &, const ProviderError &)> done);

    // Maps transport/HTTP failures to a ProviderError (401/403 → AuthExpired and reported to the
    // context, 429 → RateLimited with Retry-After, 5xx → ServiceUnavailable, …).
    ProviderError errorFor(QNetworkReply *reply) const;

private:
    ProviderContext *m_context;
};

// Shared helpers for providers.
namespace util {
QString prettyTitle(QString title);          // URL slugs / tag lists → a readable title
QString fileExtensionFor(const QUrl &url, AssetKind kind);
QString safeFileName(QString name);
int retryAfterSeconds(QNetworkReply *reply); // Retry-After or provider reset headers
} // namespace util

} // namespace cadra::assets
