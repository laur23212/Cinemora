#pragma once

// Value types shared by every part of the online asset system (providers, search, downloads,
// auth, the local asset database and the QML facade). Plain structs, copyable, no QObject.

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QNetworkRequest>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

namespace dafin::assets {

// What a panel shows. One provider can serve several kinds.
enum class AssetKind { Video, Music, SoundEffect, Sticker, Title };

QString kindToString(AssetKind kind);
AssetKind kindFromString(const QString &text, bool *ok = nullptr);
QList<AssetKind> kindsFromStrings(const QStringList &list);

// One search result, normalised across providers. `key` = "<providerId>:<id>" is unique and
// stable; everything the UI, the download manager and the database need is here.
struct AssetItem
{
    QString providerId;
    QString id;
    AssetKind kind = AssetKind::Video;
    QString title;
    QString author;
    QUrl authorUrl;
    QUrl pageUrl;             // the item's page at the provider (attribution link)
    QUrl thumbnailUrl;        // still image; for audio a waveform image or cover art
    QUrl previewUrl;          // small video/audio for hover or play, or the Lottie JSON itself
    QUrl downloadUrl;         // may be empty when the provider resolves it in download()
    QString license;          // short name, e.g. "CC BY 4.0", "Pexels License"
    QUrl licenseUrl;
    QString attribution;      // ready-made credit line when the provider gives one
    double durationSeconds = 0;
    int width = 0;
    int height = 0;
    bool downloadable = true; // false when the provider forbids download for this item
    QString downloadBlockedReason;
    QVariantMap extra;        // provider-specific fields kept for getAssetDetails()/download()

    QString key() const { return providerId + QLatin1Char(':') + id; }
    QVariantMap toVariant() const;
    static AssetItem fromVariant(const QVariantMap &map);
};

struct SearchRequest
{
    QString query;            // empty = the provider's popular/curated feed
    AssetKind kind = AssetKind::Video;
    int page = 1;             // 1-based
    int pageSize = 30;
};

struct SearchPage
{
    QList<AssetItem> items;
    bool hasMore = false;
    int totalResults = -1;    // -1 = unknown
};

// Every failure a provider can report, mapped to one message the UI can show as it is.
struct ProviderError
{
    enum class Code {
        None,
        Offline,           // no network / DNS / connection refused
        Timeout,
        RateLimited,       // 429 or provider-specific quota; see retryAfterSeconds
        AuthRequired,      // provider needs an account/key that is not set
        AuthExpired,       // stored credentials were rejected (401/403)
        Forbidden,         // e.g. download not allowed for this item
        NotFound,
        ServiceUnavailable, // 5xx
        BadResponse,       // unparseable or unexpected JSON
        Cancelled,
        Storage,           // local disk / credential store
        Unknown
    };

    Code code = Code::None;
    QString message;          // short, user-facing
    int httpStatus = 0;
    int retryAfterSeconds = 0;

    bool ok() const { return code == Code::None; }
    static ProviderError none() { return {}; }
    static ProviderError make(Code code, QString message, int httpStatus = 0, int retryAfter = 0);
    QString codeName() const;
    QVariantMap toVariant() const;
};

// How a provider authenticates. The Settings page draws its form from `fields`.
struct AuthField
{
    QString id;               // e.g. "apiKey", "clientId"
    QString label;
    bool secret = true;       // masked input, stored in the secure store
    bool optional = false;
    QString placeholder;
};

struct ProviderInfo
{
    QString id;
    QString name;
    QList<AssetKind> kinds;
    QString description;      // one line in Settings
    QString license;          // default licence summary
    QUrl homepage;
    QUrl signupUrl;           // where the user gets a free key (opened only from Settings)
    QString attributionText;  // e.g. "Videos provided by Pexels" (required by some terms)
    QUrl attributionUrl;
    bool requiresAccount = false;    // no results at all without credentials
    bool accountUnlocksMore = false; // works anonymously; an account gives more
    bool supportsOAuth = false;      // has an interactive sign-in (beginOAuth/completeOAuth)
    bool supportsRegistration = false; // can create credentials in-app (Openverse)
    QList<AuthField> fields;
    QString accountHint;      // what the account is for, shown under the form
    int responseCacheSeconds = 600;  // Pixabay requires 24 h
    int thumbnailCacheSeconds = 24 * 3600;
    bool nonCommercialOnly = false;
    // Dafin: the user's own files (My Lotties, My sounds, imported titles). Listed first in
    // "All …", never cached, and their cards carry Save / Delete.
    bool userLibrary = false;
    // False when the source has no row of its own in the panel's sidebar and its items only
    // appear under "All …" (imported .mogrt titles).
    bool ownSidebarEntry = true;
};

// Credentials as stored (JSON in the secure store). Values are opaque to everyone but the
// provider that wrote them.
using Credentials = QVariantMap;

enum class ConnectionState {
    NotRequired,   // anonymous access works and nothing is stored
    NotConnected,  // provider needs (or offers) an account and none is stored
    Connected,
    Expired,       // stored credentials were rejected; user must reconnect
    Error          // last validation failed for another reason (offline…)
};
QString connectionStateToString(ConnectionState state);

// What the download manager fetches; resolved by the provider so each one can add auth
// headers, pick a rendition or refuse.
struct DownloadSpec
{
    QNetworkRequest request;
    QString suggestedFileName; // without directory; extension included when known
    qint64 expectedBytes = -1;
};

} // namespace dafin::assets
