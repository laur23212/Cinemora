#include "app/online/library/providers/FreesoundProvider.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrlQuery>

namespace dafin::assets {

namespace {
constexpr auto kApi = "https://freesound.org/apiv2/";
const QString kFields = QStringLiteral("id,name,username,duration,license,previews,images,url,type");
} // namespace

ProviderInfo FreesoundProvider::info() const
{
    ProviderInfo i;
    i.id = QStringLiteral("freesound");
    i.name = QStringLiteral("Freesound");
    i.kinds = {AssetKind::SoundEffect};
    i.description = QObject::tr("Creative Commons sound effects and field recordings.");
    i.license = QObject::tr("Creative Commons (per sound)");
    i.homepage = QUrl(QStringLiteral("https://freesound.org/"));
    i.signupUrl = QUrl(QStringLiteral("https://freesound.org/apiv2/apply/"));
    i.attributionText = QObject::tr("Sounds from Freesound");
    i.attributionUrl = QUrl(QStringLiteral("https://freesound.org/"));
    i.requiresAccount = true;
    i.supportsOAuth = true;
    i.nonCommercialOnly = true;
    i.fields = {
        {QStringLiteral("apiKey"), QObject::tr("API key"), true, false, QObject::tr("Client secret / API key")},
        {QStringLiteral("clientId"), QObject::tr("Client ID (for original files)"), false, true, QObject::tr("Optional")},
        {QStringLiteral("clientSecret"), QObject::tr("Client secret (for original files)"), true, true, QObject::tr("Optional")},
    };
    i.accountHint = QObject::tr("Apply for a free API key on Freesound. The key alone gives high-quality MP3s; "
                                "add the client ID and secret, then Sign in, to download original files. "
                                "The Freesound API is free for non-commercial use.");
    i.responseCacheSeconds = 1800;
    return i;
}

QString FreesoundProvider::licenseName(const QString &url)
{
    const QString u = url.toLower();
    if (u.contains(QLatin1String("publicdomain/zero")))
        return QStringLiteral("CC0 1.0");
    if (u.contains(QLatin1String("sampling+")))
        return QStringLiteral("Sampling+");
    QString version;
    const QStringList parts = u.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &p : parts) {
        if (!p.isEmpty() && p.at(0).isDigit())
            version = p;
    }
    if (u.contains(QLatin1String("/by-nc/")))
        return QStringLiteral("CC BY-NC ") + version;
    if (u.contains(QLatin1String("/by/")))
        return QStringLiteral("CC BY ") + version;
    return url.isEmpty() ? QStringLiteral("Creative Commons") : url;
}

SearchPage FreesoundProvider::parse(const QJsonDocument &doc)
{
    SearchPage page;
    const QJsonObject root = doc.object();
    for (const QJsonValue &v : root.value(QStringLiteral("results")).toArray()) {
        const QJsonObject o = v.toObject();
        const QJsonObject previews = o.value(QStringLiteral("previews")).toObject();
        const QString hq = previews.value(QStringLiteral("preview-hq-mp3")).toString();
        if (hq.isEmpty())
            continue;
        AssetItem a;
        a.providerId = QStringLiteral("freesound");
        a.id = QString::number(o.value(QStringLiteral("id")).toInteger());
        a.kind = AssetKind::SoundEffect;
        a.title = o.value(QStringLiteral("name")).toString();
        // Strip the extension Freesound names often carry ("door slam.wav").
        const int dot = a.title.lastIndexOf(QLatin1Char('.'));
        if (dot > 0 && a.title.size() - dot <= 5)
            a.title.truncate(dot);
        a.author = o.value(QStringLiteral("username")).toString();
        a.authorUrl = QUrl(QStringLiteral("https://freesound.org/people/%1/").arg(a.author));
        a.pageUrl = QUrl(o.value(QStringLiteral("url")).toString());
        a.thumbnailUrl = QUrl(o.value(QStringLiteral("images")).toObject().value(QStringLiteral("waveform_m")).toString());
        a.previewUrl = QUrl(previews.value(QStringLiteral("preview-lq-mp3")).toString(hq));
        a.downloadUrl = QUrl(hq);
        a.durationSeconds = o.value(QStringLiteral("duration")).toDouble();
        const QString licenseUrl = o.value(QStringLiteral("license")).toString();
        a.licenseUrl = QUrl(licenseUrl);
        a.license = licenseName(licenseUrl);
        a.attribution = QObject::tr("\"%1\" by %2 (freesound.org), %3").arg(a.title, a.author, a.license);
        a.extra.insert(QStringLiteral("originalType"), o.value(QStringLiteral("type")).toString());
        page.items.append(a);
    }
    page.totalResults = root.value(QStringLiteral("count")).toInt(-1);
    page.hasMore = !root.value(QStringLiteral("next")).toString().isEmpty();
    return page;
}

RequestHandle FreesoundProvider::search(const SearchRequest &request, SearchCallback callback)
{
    const QString key = credentials().value(QStringLiteral("apiKey")).toString();
    if (key.isEmpty()) {
        callback({}, ProviderError::make(ProviderError::Code::AuthRequired,
                                         QObject::tr("Connect Freesound in Settings to browse its sounds.")));
        return {};
    }
    QUrl url(QString::fromLatin1(kApi) + QStringLiteral("search/text/"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("query"), request.query.trimmed());
    if (request.query.trimmed().isEmpty())
        q.addQueryItem(QStringLiteral("sort"), QStringLiteral("downloads_desc"));
    q.addQueryItem(QStringLiteral("page"), QString::number(qMax(1, request.page)));
    q.addQueryItem(QStringLiteral("page_size"), QString::number(qBound(1, request.pageSize, 150)));
    q.addQueryItem(QStringLiteral("fields"), kFields);
    q.addQueryItem(QStringLiteral("token"), key);
    url.setQuery(q);
    return getJson(makeRequest(url), [callback](const QJsonDocument &doc, const ProviderError &error) {
        if (!error.ok()) {
            callback({}, error);
            return;
        }
        callback(parse(doc), ProviderError::none());
    });
}

QUrl FreesoundProvider::oauthAuthorizationUrl(const Credentials &partial) const
{
    const QString clientId = partial.value(QStringLiteral("clientId")).toString();
    if (clientId.isEmpty())
        return {};
    QUrl url(QString::fromLatin1(kApi) + QStringLiteral("oauth2/authorize/"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("client_id"), clientId);
    q.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    q.addQueryItem(QStringLiteral("state"), QStringLiteral("dafin"));
    url.setQuery(q);
    return url;
}

RequestHandle FreesoundProvider::requestToken(const Credentials &base, const QList<QPair<QString, QString>> &form,
                                              AuthCallback callback)
{
    QNetworkRequest r = makeRequest(QUrl(QString::fromLatin1(kApi) + QStringLiteral("oauth2/access_token/")));
    r.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    QUrlQuery body;
    body.addQueryItem(QStringLiteral("client_id"), base.value(QStringLiteral("clientId")).toString());
    body.addQueryItem(QStringLiteral("client_secret"), base.value(QStringLiteral("clientSecret")).toString());
    for (const auto &kv : form)
        body.addQueryItem(kv.first, kv.second);
    RequestHandle handle;
    QNetworkReply *reply = context()->network()->post(r, body.toString(QUrl::FullyEncoded).toUtf8());
    handle.attach(reply);
    const auto cancelled = handle.flag();
    QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply, cancelled, base, callback]() {
        reply->deleteLater();
        if (*cancelled)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 400 || status == 401)
                callback({}, ProviderError::make(ProviderError::Code::AuthExpired,
                                                 QObject::tr("Freesound did not accept the sign-in. Start it again."), status));
            else
                callback({}, errorFor(reply));
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        const QString access = o.value(QStringLiteral("access_token")).toString();
        if (access.isEmpty()) {
            callback({}, ProviderError::make(ProviderError::Code::BadResponse, QObject::tr("Freesound sent no access token.")));
            return;
        }
        Credentials creds = base;
        creds.insert(QStringLiteral("accessToken"), access);
        creds.insert(QStringLiteral("refreshToken"),
                     o.value(QStringLiteral("refresh_token")).toString(base.value(QStringLiteral("refreshToken")).toString()));
        const qint64 expiresIn = o.value(QStringLiteral("expires_in")).toInteger(86400);
        creds.insert(QStringLiteral("expiresAt"), QDateTime::currentSecsSinceEpoch() + expiresIn - 60);
        callback(creds, ProviderError::none());
    });
    return handle;
}

RequestHandle FreesoundProvider::authenticate(const QVariantMap &input, AuthCallback callback)
{
    const QString code = input.value(QStringLiteral("code")).toString().trimmed();
    if (code.isEmpty())
        return AssetProvider::authenticate(input, std::move(callback)); // key (+ optional client) check
    Credentials base = credentials();
    if (base.value(QStringLiteral("clientId")).toString().isEmpty()
        || base.value(QStringLiteral("clientSecret")).toString().isEmpty()) {
        callback({}, ProviderError::make(ProviderError::Code::AuthRequired,
                                         QObject::tr("Save the Freesound client ID and secret first.")));
        return {};
    }
    return requestToken(base, {{QStringLiteral("grant_type"), QStringLiteral("authorization_code")},
                               {QStringLiteral("code"), code}}, std::move(callback));
}

RequestHandle FreesoundProvider::download(const AssetItem &item, DownloadSpecCallback callback)
{
    const Credentials creds = credentials();
    const QString access = creds.value(QStringLiteral("accessToken")).toString();
    if (access.isEmpty())
        return AssetProvider::download(item, std::move(callback)); // HQ MP3 preview, no OAuth needed

    auto fetchOriginal = [this, item, callback](const QString &token) {
        DownloadSpec spec;
        spec.request = makeRequest(QUrl(QString::fromLatin1(kApi) + QStringLiteral("sounds/%1/download/").arg(item.id)));
        spec.request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
        spec.request.setRawHeader("Accept", "*/*");
        spec.request.setTransferTimeout(0);
        QString ext = item.extra.value(QStringLiteral("originalType")).toString();
        if (ext.isEmpty())
            ext = QStringLiteral("wav");
        spec.suggestedFileName = util::safeFileName(item.title) + QStringLiteral(" [") + item.id + QStringLiteral("].") + ext;
        callback(spec, ProviderError::none());
    };

    if (creds.value(QStringLiteral("expiresAt")).toLongLong() > QDateTime::currentSecsSinceEpoch()) {
        fetchOriginal(access);
        return {};
    }
    // Token expired: refresh once, store the new tokens, then download.
    const QString refresh = creds.value(QStringLiteral("refreshToken")).toString();
    return requestToken(creds, {{QStringLiteral("grant_type"), QStringLiteral("refresh_token")},
                                {QStringLiteral("refresh_token"), refresh}},
                        [this, item, callback, fetchOriginal](const Credentials &fresh, const ProviderError &error) {
                            if (!error.ok()) {
                                // Sign-in lapsed: fall back to the HQ preview rather than failing.
                                Credentials keyOnly = credentials();
                                keyOnly.remove(QStringLiteral("accessToken"));
                                keyOnly.remove(QStringLiteral("refreshToken"));
                                keyOnly.remove(QStringLiteral("expiresAt"));
                                // The API key still works, so the provider stays connected;
                                // only original-quality downloads need a new sign-in.
                                context()->updateCredentials(info().id, keyOnly);
                                AssetProvider::download(item, callback);
                                return;
                            }
                            context()->updateCredentials(info().id, fresh);
                            fetchOriginal(fresh.value(QStringLiteral("accessToken")).toString());
                        });
}

} // namespace dafin::assets
