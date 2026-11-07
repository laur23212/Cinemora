#include "app/online/library/providers/JamendoProvider.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QUrlQuery>

namespace dafin::assets {

ProviderInfo JamendoProvider::info() const
{
    ProviderInfo i;
    i.id = QStringLiteral("jamendo");
    i.name = QStringLiteral("Jamendo");
    i.kinds = {AssetKind::Music};
    i.description = QObject::tr("Independent music under Creative Commons licences.");
    i.license = QObject::tr("Creative Commons (per track)");
    i.homepage = QUrl(QStringLiteral("https://www.jamendo.com/"));
    i.signupUrl = QUrl(QStringLiteral("https://devportal.jamendo.com/"));
    i.attributionText = QObject::tr("Music from Jamendo");
    i.attributionUrl = QUrl(QStringLiteral("https://www.jamendo.com/"));
    i.requiresAccount = true;
    i.fields = {{QStringLiteral("clientId"), QObject::tr("Client ID"), true, false, QObject::tr("Paste your Jamendo client_id")}};
    i.accountHint = QObject::tr("Create a free developer account on Jamendo and add an application to get a client ID. "
                                "Only tracks whose artists allow downloads can be downloaded.");
    i.responseCacheSeconds = 3600;
    return i;
}

SearchPage JamendoProvider::parse(const QJsonDocument &doc, int pageSize, ProviderError *error)
{
    SearchPage page;
    const QJsonObject root = doc.object();
    const QJsonObject headers = root.value(QStringLiteral("headers")).toObject();
    if (headers.value(QStringLiteral("status")).toString() == QLatin1String("failed")) {
        const QString message = headers.value(QStringLiteral("error_message")).toString();
        if (error) {
            const bool keyProblem = message.contains(QLatin1String("client"), Qt::CaseInsensitive)
                                    || message.contains(QLatin1String("credential"), Qt::CaseInsensitive);
            *error = ProviderError::make(keyProblem ? ProviderError::Code::AuthExpired : ProviderError::Code::BadResponse,
                                         keyProblem ? QObject::tr("Jamendo did not accept the client ID. Check it in Settings.")
                                                    : QObject::tr("Jamendo: %1").arg(message));
        }
        return page;
    }
    for (const QJsonValue &v : root.value(QStringLiteral("results")).toArray()) {
        const QJsonObject o = v.toObject();
        AssetItem a;
        a.providerId = QStringLiteral("jamendo");
        a.id = o.value(QStringLiteral("id")).toVariant().toString();
        a.kind = AssetKind::Music;
        a.title = o.value(QStringLiteral("name")).toString();
        a.author = o.value(QStringLiteral("artist_name")).toString();
        a.pageUrl = QUrl(o.value(QStringLiteral("shareurl")).toString());
        QString cover = o.value(QStringLiteral("image")).toString();
        if (cover.isEmpty())
            cover = o.value(QStringLiteral("album_image")).toString();
        a.thumbnailUrl = QUrl(cover);
        a.previewUrl = QUrl(o.value(QStringLiteral("audio")).toString());
        a.downloadUrl = QUrl(o.value(QStringLiteral("audiodownload")).toString());
        a.durationSeconds = o.value(QStringLiteral("duration")).toVariant().toDouble();
        // Missing field (older API answers) = treat as not allowed: only offer what is allowed.
        a.downloadable = o.value(QStringLiteral("audiodownload_allowed")).toBool(false) && a.downloadUrl.isValid();
        if (!a.downloadable)
            a.downloadBlockedReason = QObject::tr("The artist does not allow downloading this track. You can still preview it.");
        const QString licenseUrl = o.value(QStringLiteral("license_ccurl")).toString();
        a.licenseUrl = QUrl(licenseUrl);
        a.license = licenseUrl.contains(QLatin1String("by-nc")) ? QStringLiteral("CC BY-NC")
                  : licenseUrl.contains(QLatin1String("by-sa")) ? QStringLiteral("CC BY-SA")
                  : licenseUrl.contains(QLatin1String("by-nd")) ? QStringLiteral("CC BY-ND")
                  : licenseUrl.contains(QLatin1String("/by/")) ? QStringLiteral("CC BY")
                  : QStringLiteral("Creative Commons");
        a.attribution = QObject::tr("\"%1\" by %2 (Jamendo), %3").arg(a.title, a.author, a.license);
        // Waveform peaks when the API includes them (a JSON string of {"peaks":[…]}).
        const QJsonValue wave = o.value(QStringLiteral("waveform"));
        if (wave.isString()) {
            const QJsonArray peaks = QJsonDocument::fromJson(wave.toString().toUtf8()).object()
                                         .value(QStringLiteral("peaks")).toArray();
            if (!peaks.isEmpty())
                a.extra.insert(QStringLiteral("peaks"), peaks.toVariantList());
        }
        page.items.append(a);
    }
    const int total = headers.value(QStringLiteral("results_count")).toInt(-1);
    page.hasMore = page.items.size() >= pageSize;
    page.totalResults = total;
    return page;
}

RequestHandle JamendoProvider::search(const SearchRequest &request, SearchCallback callback)
{
    const QString clientId = credentials().value(QStringLiteral("clientId")).toString();
    if (clientId.isEmpty()) {
        callback({}, ProviderError::make(ProviderError::Code::AuthRequired,
                                         QObject::tr("Connect Jamendo in Settings to browse its music.")));
        return {};
    }
    const int limit = qBound(1, request.pageSize, 200);
    QUrl url(QStringLiteral("https://api.jamendo.com/v3.0/tracks/"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("client_id"), clientId);
    q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    q.addQueryItem(QStringLiteral("limit"), QString::number(limit));
    q.addQueryItem(QStringLiteral("offset"), QString::number((qMax(1, request.page) - 1) * limit));
    q.addQueryItem(QStringLiteral("audioformat"), QStringLiteral("mp32"));
    q.addQueryItem(QStringLiteral("imagesize"), QStringLiteral("200"));
    q.addQueryItem(QStringLiteral("order"), QStringLiteral("popularity_total"));
    if (!request.query.trimmed().isEmpty())
        q.addQueryItem(QStringLiteral("search"), request.query.trimmed());
    url.setQuery(q);
    return getJson(makeRequest(url), [this, callback, limit](const QJsonDocument &doc, const ProviderError &error) {
        if (!error.ok()) {
            callback({}, error);
            return;
        }
        ProviderError parseError;
        const SearchPage page = parse(doc, limit, &parseError);
        if (parseError.code == ProviderError::Code::AuthExpired && !credentials().isEmpty())
            context()->reportAuthFailure(info().id, parseError);
        callback(page, parseError);
    });
}

} // namespace dafin::assets
