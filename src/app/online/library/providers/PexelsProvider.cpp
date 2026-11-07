#include "app/online/library/providers/PexelsProvider.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QUrlQuery>

namespace cadra::assets {

namespace {
constexpr auto kApi = "https://api.pexels.com/v1/videos/";

// Pick the rendition whose width is the largest not above `maxWidth` (or the smallest when
// none fits) so previews stay light and downloads stay at 1080p.
QJsonObject pickFile(const QJsonArray &files, int maxWidth)
{
    QJsonObject best;
    QJsonObject smallest;
    for (const QJsonValue &v : files) {
        const QJsonObject f = v.toObject();
        if (!f.value(QStringLiteral("file_type")).toString().contains(QLatin1String("mp4"))
            && !f.value(QStringLiteral("link")).toString().contains(QLatin1String(".mp4")))
            continue;
        const int w = f.value(QStringLiteral("width")).toInt();
        if (w <= maxWidth && w > best.value(QStringLiteral("width")).toInt())
            best = f;
        if (smallest.isEmpty() || w < smallest.value(QStringLiteral("width")).toInt())
            smallest = f;
    }
    return best.isEmpty() ? smallest : best;
}
} // namespace

ProviderInfo PexelsProvider::info() const
{
    ProviderInfo i;
    i.id = QStringLiteral("pexels");
    i.name = QStringLiteral("Pexels");
    i.kinds = {AssetKind::Video};
    i.description = QObject::tr("Free stock videos, free for commercial use.");
    i.license = QStringLiteral("Pexels License");
    i.homepage = QUrl(QStringLiteral("https://www.pexels.com/videos/"));
    i.signupUrl = QUrl(QStringLiteral("https://www.pexels.com/api/new/"));
    i.attributionText = QObject::tr("Videos provided by Pexels");
    i.attributionUrl = QUrl(QStringLiteral("https://www.pexels.com"));
    i.requiresAccount = true;
    i.fields = {{QStringLiteral("apiKey"), QObject::tr("API key"), true, false, QObject::tr("Paste your Pexels API key")}};
    i.accountHint = QObject::tr("Create a free Pexels account and request an API key; it is issued instantly.");
    i.responseCacheSeconds = 3600;
    return i;
}

SearchPage PexelsProvider::parse(const QJsonDocument &doc, int pageSize)
{
    SearchPage page;
    const QJsonObject root = doc.object();
    for (const QJsonValue &v : root.value(QStringLiteral("videos")).toArray()) {
        const QJsonObject o = v.toObject();
        const QJsonArray files = o.value(QStringLiteral("video_files")).toArray();
        const QJsonObject preview = pickFile(files, 640);
        const QJsonObject full = pickFile(files, 1920);
        if (full.isEmpty())
            continue;
        AssetItem a;
        a.providerId = QStringLiteral("pexels");
        a.id = QString::number(o.value(QStringLiteral("id")).toInteger());
        a.kind = AssetKind::Video;
        a.pageUrl = QUrl(o.value(QStringLiteral("url")).toString());
        a.title = util::prettyTitle(o.value(QStringLiteral("url")).toString());
        if (a.title.isEmpty())
            a.title = QObject::tr("Pexels video %1").arg(a.id);
        const QJsonObject user = o.value(QStringLiteral("user")).toObject();
        a.author = user.value(QStringLiteral("name")).toString();
        a.authorUrl = QUrl(user.value(QStringLiteral("url")).toString());
        a.thumbnailUrl = QUrl(o.value(QStringLiteral("image")).toString());
        a.previewUrl = QUrl(preview.value(QStringLiteral("link")).toString());
        a.downloadUrl = QUrl(full.value(QStringLiteral("link")).toString());
        a.width = full.value(QStringLiteral("width")).toInt(o.value(QStringLiteral("width")).toInt());
        a.height = full.value(QStringLiteral("height")).toInt(o.value(QStringLiteral("height")).toInt());
        a.durationSeconds = o.value(QStringLiteral("duration")).toDouble();
        a.license = QStringLiteral("Pexels License");
        a.licenseUrl = QUrl(QStringLiteral("https://www.pexels.com/license/"));
        a.attribution = a.author.isEmpty() ? QObject::tr("Video from Pexels")
                                           : QObject::tr("Video by %1 on Pexels").arg(a.author);
        a.extra.insert(QStringLiteral("sourceWidth"), o.value(QStringLiteral("width")).toInt());
        a.extra.insert(QStringLiteral("sourceHeight"), o.value(QStringLiteral("height")).toInt());
        page.items.append(a);
    }
    page.totalResults = root.value(QStringLiteral("total_results")).toInt(-1);
    page.hasMore = root.contains(QStringLiteral("next_page"))
                   || (page.items.size() >= pageSize && pageSize > 0);
    return page;
}

RequestHandle PexelsProvider::search(const SearchRequest &request, SearchCallback callback)
{
    const QString key = credentials().value(QStringLiteral("apiKey")).toString();
    if (key.isEmpty()) {
        callback({}, ProviderError::make(ProviderError::Code::AuthRequired,
                                         QObject::tr("Connect Pexels in Settings to browse its videos.")));
        return {};
    }
    const int perPage = qBound(1, request.pageSize, 80);
    QUrl url(QString::fromLatin1(kApi) + (request.query.trimmed().isEmpty() ? QStringLiteral("popular") : QStringLiteral("search")));
    QUrlQuery q;
    if (!request.query.trimmed().isEmpty())
        q.addQueryItem(QStringLiteral("query"), request.query.trimmed());
    q.addQueryItem(QStringLiteral("per_page"), QString::number(perPage));
    q.addQueryItem(QStringLiteral("page"), QString::number(qMax(1, request.page)));
    url.setQuery(q);
    QNetworkRequest r = makeRequest(url);
    r.setRawHeader("Authorization", key.toUtf8());
    return getJson(r, [callback, perPage](const QJsonDocument &doc, const ProviderError &error) {
        if (!error.ok()) {
            callback({}, error);
            return;
        }
        callback(parse(doc, perPage), ProviderError::none());
    });
}

RequestHandle PexelsProvider::download(const AssetItem &item, DownloadSpecCallback callback)
{
    // Pexels file links are public CDN URLs; no key on the download itself.
    return AssetProvider::download(item, std::move(callback));
}

} // namespace cadra::assets
