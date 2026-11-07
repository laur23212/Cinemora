#include "app/online/library/providers/OpenverseProvider.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrlQuery>

namespace cadra::assets {

namespace {
constexpr auto kApi = "https://api.openverse.org/v1/";
constexpr int kAnonymousPageSize = 20; // anonymous page size cap
} // namespace

ProviderInfo OpenverseProvider::info() const
{
    ProviderInfo i;
    i.id = QStringLiteral("openverse");
    i.name = QStringLiteral("Openverse");
    i.kinds = {AssetKind::Music, AssetKind::SoundEffect};
    i.description = QObject::tr("Openly licensed music and sound effects from many archives.");
    i.license = QObject::tr("Creative Commons / public domain (per item)");
    i.homepage = QUrl(QStringLiteral("https://openverse.org/"));
    i.signupUrl = QUrl(QStringLiteral("https://api.openverse.org/v1/#tag/auth"));
    i.attributionText = QObject::tr("Audio found with Openverse");
    i.attributionUrl = QUrl(QStringLiteral("https://openverse.org/"));
    i.requiresAccount = false;
    i.accountUnlocksMore = true;
    i.supportsRegistration = true;
    i.fields = {
        {QStringLiteral("clientId"), QObject::tr("Client ID"), true, true, QObject::tr("From a registered Openverse app")},
        {QStringLiteral("clientSecret"), QObject::tr("Client secret"), true, true, QString()},
        {QStringLiteral("email"), QObject::tr("Email (to register)"), false, true, QObject::tr("Only to create new credentials")},
    };
    i.accountHint = QObject::tr("Works without an account. Register (name + email) to get higher rate limits; "
                                "Openverse emails a link you must open once before the credentials work.");
    i.responseCacheSeconds = 1800;
    return i;
}

SearchPage OpenverseProvider::parse(const QJsonDocument &doc, AssetKind kind)
{
    SearchPage page;
    const QJsonObject root = doc.object();
    for (const QJsonValue &v : root.value(QStringLiteral("results")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString fileUrl = o.value(QStringLiteral("url")).toString();
        if (fileUrl.isEmpty())
            continue;
        AssetItem a;
        a.providerId = QStringLiteral("openverse");
        a.id = o.value(QStringLiteral("id")).toString();
        a.kind = kind;
        a.title = o.value(QStringLiteral("title")).toString();
        if (a.title.isEmpty())
            a.title = QObject::tr("Untitled");
        a.author = o.value(QStringLiteral("creator")).toString();
        a.authorUrl = QUrl(o.value(QStringLiteral("creator_url")).toString());
        a.pageUrl = QUrl(o.value(QStringLiteral("foreign_landing_url")).toString());
        a.thumbnailUrl = QUrl(o.value(QStringLiteral("thumbnail")).toString());
        a.previewUrl = QUrl(fileUrl);
        a.downloadUrl = QUrl(fileUrl);
        // Openverse reports audio duration in milliseconds.
        a.durationSeconds = o.value(QStringLiteral("duration")).toDouble() / 1000.0;
        const QString lic = o.value(QStringLiteral("license")).toString();
        const QString ver = o.value(QStringLiteral("license_version")).toString();
        a.license = lic == QLatin1String("cc0") ? QStringLiteral("CC0 1.0")
                  : lic == QLatin1String("pdm") ? QStringLiteral("Public Domain Mark")
                  : QStringLiteral("CC %1 %2").arg(lic.toUpper(), ver).trimmed();
        a.licenseUrl = QUrl(o.value(QStringLiteral("license_url")).toString());
        a.attribution = o.value(QStringLiteral("attribution")).toString();
        if (a.attribution.isEmpty())
            a.attribution = QObject::tr("\"%1\" by %2, %3").arg(a.title, a.author, a.license);
        a.extra.insert(QStringLiteral("source"), o.value(QStringLiteral("source")).toString());
        a.extra.insert(QStringLiteral("filetype"), o.value(QStringLiteral("filetype")).toString());
        page.items.append(a);
    }
    const int pageNo = root.value(QStringLiteral("page")).toInt(1);
    const int pageCount = root.value(QStringLiteral("page_count")).toInt(0);
    page.totalResults = root.value(QStringLiteral("result_count")).toInt(-1);
    page.hasMore = pageNo < pageCount;
    return page;
}

RequestHandle OpenverseProvider::fetchToken(const Credentials &creds, AuthCallback callback)
{
    QNetworkRequest r = makeRequest(QUrl(QString::fromLatin1(kApi) + QStringLiteral("auth_tokens/token/")));
    r.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    QUrlQuery body;
    body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("client_credentials"));
    body.addQueryItem(QStringLiteral("client_id"), creds.value(QStringLiteral("clientId")).toString());
    body.addQueryItem(QStringLiteral("client_secret"), creds.value(QStringLiteral("clientSecret")).toString());
    RequestHandle handle;
    QNetworkReply *reply = context()->network()->post(r, body.toString(QUrl::FullyEncoded).toUtf8());
    handle.attach(reply);
    const auto cancelled = handle.flag();
    QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply, cancelled, creds, callback]() {
        reply->deleteLater();
        if (*cancelled)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            if (status == 400 || status == 401)
                callback({}, ProviderError::make(ProviderError::Code::AuthExpired,
                                                 QObject::tr("Openverse did not accept these credentials. If you just registered, "
                                                             "open the link Openverse emailed you, then press Reconnect."),
                                                 status));
            else
                callback({}, errorFor(reply));
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        Credentials out = creds;
        out.insert(QStringLiteral("accessToken"), o.value(QStringLiteral("access_token")).toString());
        out.insert(QStringLiteral("expiresAt"),
                   QDateTime::currentSecsSinceEpoch() + o.value(QStringLiteral("expires_in")).toInteger(36000) - 60);
        callback(out, ProviderError::none());
    });
    return handle;
}

RequestHandle OpenverseProvider::authenticate(const QVariantMap &input, AuthCallback callback)
{
    const QString clientId = input.value(QStringLiteral("clientId")).toString().trimmed();
    const QString clientSecret = input.value(QStringLiteral("clientSecret")).toString().trimmed();
    if (!clientId.isEmpty() && !clientSecret.isEmpty())
        return fetchToken({{QStringLiteral("clientId"), clientId}, {QStringLiteral("clientSecret"), clientSecret}}, callback);

    // Register a new application for this user (name + email); credentials come back at once
    // but only work after the user verifies the email Openverse sends.
    const QString email = input.value(QStringLiteral("email")).toString().trimmed();
    if (!email.contains(QLatin1Char('@'))) {
        callback({}, ProviderError::make(ProviderError::Code::AuthRequired,
                                         QObject::tr("Enter a client ID and secret, or an email address to register.")));
        return {};
    }
    QNetworkRequest r = makeRequest(QUrl(QString::fromLatin1(kApi) + QStringLiteral("auth_tokens/register/")));
    r.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QByteArray body = QJsonDocument(QJsonObject{
        {QStringLiteral("name"), QStringLiteral("Cadra video editor (%1)").arg(email.section(QLatin1Char('@'), 0, 0))},
        {QStringLiteral("description"), QStringLiteral("Personal use of the Cadra desktop video editor: in-app search of openly licensed audio.")},
        {QStringLiteral("email"), email}}).toJson(QJsonDocument::Compact);
    RequestHandle handle;
    QNetworkReply *reply = context()->network()->post(r, body);
    handle.attach(reply);
    const auto cancelled = handle.flag();
    QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply, cancelled, callback]() {
        reply->deleteLater();
        if (*cancelled)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            callback({}, errorFor(reply));
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        const Credentials creds{{QStringLiteral("clientId"), o.value(QStringLiteral("client_id")).toString()},
                                {QStringLiteral("clientSecret"), o.value(QStringLiteral("client_secret")).toString()}};
        if (creds.value(QStringLiteral("clientId")).toString().isEmpty()) {
            callback({}, ProviderError::make(ProviderError::Code::BadResponse, QObject::tr("Openverse did not return credentials.")));
            return;
        }
        // Keep them even though they are not verified yet: Reconnect later finishes the job.
        callback(creds, ProviderError::make(ProviderError::Code::AuthExpired,
                                            QObject::tr("Registered. Open the link Openverse emailed you, then press Reconnect.")));
    });
    return handle;
}

void OpenverseProvider::logout()
{
    // Tokens are short-lived and not revocable through the API; dropping them is enough.
}

RequestHandle OpenverseProvider::withToken(std::function<void(const QString &, const ProviderError &)> then)
{
    const Credentials creds = credentials();
    if (creds.value(QStringLiteral("clientId")).toString().isEmpty()) {
        then(QString(), ProviderError::none());
        return {};
    }
    if (creds.value(QStringLiteral("expiresAt")).toLongLong() > QDateTime::currentSecsSinceEpoch()
        && !creds.value(QStringLiteral("accessToken")).toString().isEmpty()) {
        then(creds.value(QStringLiteral("accessToken")).toString(), ProviderError::none());
        return {};
    }
    return fetchToken(creds, [this, then](const Credentials &fresh, const ProviderError &error) {
        if (!error.ok()) {
            context()->reportAuthFailure(info().id, error);
            then(QString(), ProviderError::none()); // fall back to anonymous access
            return;
        }
        context()->updateCredentials(info().id, fresh);
        then(fresh.value(QStringLiteral("accessToken")).toString(), ProviderError::none());
    });
}

RequestHandle OpenverseProvider::search(const SearchRequest &request, SearchCallback callback)
{
    // One cancel flag covers both steps (token, then search): a cancelled search never calls back.
    RequestHandle result;
    const std::shared_ptr<bool> cancelled = result.flag();
    const SearchRequest req = request;
    withToken([this, req, callback, cancelled](const QString &token, const ProviderError &) {
        if (*cancelled)
            return;
        QUrl url(QString::fromLatin1(kApi) + QStringLiteral("audio/"));
        QUrlQuery q;
        QString query = req.query.trimmed();
        // An empty feed is not offered; seed it with a term that matches the panel.
        if (query.isEmpty())
            query = req.kind == AssetKind::Music ? QStringLiteral("instrumental") : QStringLiteral("sound effect");
        q.addQueryItem(QStringLiteral("q"), query);
        q.addQueryItem(QStringLiteral("category"), req.kind == AssetKind::Music ? QStringLiteral("music")
                                                                                : QStringLiteral("sound_effect"));
        q.addQueryItem(QStringLiteral("page"), QString::number(qMax(1, req.page)));
        q.addQueryItem(QStringLiteral("page_size"),
                       QString::number(token.isEmpty() ? qMin(req.pageSize, kAnonymousPageSize) : qBound(1, req.pageSize, 50)));
        q.addQueryItem(QStringLiteral("mature"), QStringLiteral("false"));
        url.setQuery(q);
        QNetworkRequest r = makeRequest(url);
        if (!token.isEmpty())
            r.setRawHeader("Authorization", "Bearer " + token.toUtf8());
        const AssetKind kind = req.kind;
        getJson(r, [callback, kind, cancelled](const QJsonDocument &doc, const ProviderError &error) {
            if (*cancelled)
                return;
            if (!error.ok()) {
                callback({}, error);
                return;
            }
            callback(parse(doc, kind), ProviderError::none());
        });
    });
    return result;
}

RequestHandle OpenverseProvider::getAssetDetails(const AssetItem &item, DetailsCallback callback)
{
    // Waveform peaks for audio cards that have no cover image.
    QUrl url(QString::fromLatin1(kApi) + QStringLiteral("audio/%1/waveform/").arg(item.id));
    return getJson(makeRequest(url), [item, callback](const QJsonDocument &doc, const ProviderError &error) {
        AssetItem out = item;
        if (error.ok()) {
            const QJsonArray points = doc.object().value(QStringLiteral("points")).toArray();
            if (!points.isEmpty())
                out.extra.insert(QStringLiteral("peaks"), points.toVariantList());
        }
        callback(out, ProviderError::none()); // a missing waveform is not an error
    });
}

} // namespace cadra::assets
