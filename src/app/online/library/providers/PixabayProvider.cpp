#include "app/online/library/providers/PixabayProvider.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QUrlQuery>

namespace cadra::assets {

ProviderInfo PixabayProvider::info() const
{
    ProviderInfo i;
    i.id = QStringLiteral("pixabay");
    i.name = QStringLiteral("Pixabay");
    i.kinds = {AssetKind::Video};
    i.description = QObject::tr("Free stock videos under the Pixabay Content License.");
    i.license = QStringLiteral("Pixabay Content License");
    i.homepage = QUrl(QStringLiteral("https://pixabay.com/videos/"));
    i.signupUrl = QUrl(QStringLiteral("https://pixabay.com/api/docs/"));
    i.attributionText = QObject::tr("Videos from Pixabay");
    i.attributionUrl = QUrl(QStringLiteral("https://pixabay.com/"));
    i.requiresAccount = true;
    i.fields = {{QStringLiteral("apiKey"), QObject::tr("API key"), true, false, QObject::tr("Paste your Pixabay API key")}};
    i.accountHint = QObject::tr("Sign in to Pixabay; your key is shown on the API documentation page.");
    i.responseCacheSeconds = 24 * 3600; // required by the Pixabay API terms
    return i;
}

SearchPage PixabayProvider::parse(const QJsonDocument &doc, int pageNo, int pageSize)
{
    SearchPage page;
    const QJsonObject root = doc.object();
    for (const QJsonValue &v : root.value(QStringLiteral("hits")).toArray()) {
        const QJsonObject o = v.toObject();
        const QJsonObject videos = o.value(QStringLiteral("videos")).toObject();
        const QJsonObject tiny = videos.value(QStringLiteral("tiny")).toObject();
        const QJsonObject small = videos.value(QStringLiteral("small")).toObject();
        QJsonObject full = videos.value(QStringLiteral("large")).toObject();
        // "large" is empty for clips without a 1080p rendition; fall back to "medium".
        if (full.value(QStringLiteral("url")).toString().isEmpty())
            full = videos.value(QStringLiteral("medium")).toObject();
        if (full.value(QStringLiteral("url")).toString().isEmpty())
            continue;
        AssetItem a;
        a.providerId = QStringLiteral("pixabay");
        a.id = QString::number(o.value(QStringLiteral("id")).toInteger());
        a.kind = AssetKind::Video;
        a.title = util::prettyTitle(o.value(QStringLiteral("tags")).toString());
        if (a.title.isEmpty())
            a.title = QObject::tr("Pixabay video %1").arg(a.id);
        a.author = o.value(QStringLiteral("user")).toString();
        if (!a.author.isEmpty())
            a.authorUrl = QUrl(QStringLiteral("https://pixabay.com/users/%1-%2/")
                                   .arg(a.author).arg(o.value(QStringLiteral("user_id")).toInteger()));
        a.pageUrl = QUrl(o.value(QStringLiteral("pageURL")).toString());
        QString thumb = tiny.value(QStringLiteral("thumbnail")).toString();
        if (thumb.isEmpty())
            thumb = small.value(QStringLiteral("thumbnail")).toString();
        a.thumbnailUrl = QUrl(thumb);
        a.previewUrl = QUrl(tiny.value(QStringLiteral("url")).toString());
        a.downloadUrl = QUrl(full.value(QStringLiteral("url")).toString());
        a.width = full.value(QStringLiteral("width")).toInt();
        a.height = full.value(QStringLiteral("height")).toInt();
        a.durationSeconds = o.value(QStringLiteral("duration")).toDouble();
        a.license = QStringLiteral("Pixabay Content License");
        a.licenseUrl = QUrl(QStringLiteral("https://pixabay.com/service/license-summary/"));
        a.attribution = a.author.isEmpty() ? QObject::tr("Video from Pixabay")
                                           : QObject::tr("Video by %1 from Pixabay").arg(a.author);
        a.extra.insert(QStringLiteral("bytes"), full.value(QStringLiteral("size")).toInteger());
        page.items.append(a);
    }
    const int totalHits = root.value(QStringLiteral("totalHits")).toInt(-1);
    page.totalResults = totalHits;
    page.hasMore = totalHits >= 0 ? pageNo * pageSize < totalHits : page.items.size() >= pageSize;
    return page;
}

RequestHandle PixabayProvider::search(const SearchRequest &request, SearchCallback callback)
{
    const QString key = credentials().value(QStringLiteral("apiKey")).toString();
    if (key.isEmpty()) {
        callback({}, ProviderError::make(ProviderError::Code::AuthRequired,
                                         QObject::tr("Connect Pixabay in Settings to browse its videos.")));
        return {};
    }
    const int perPage = qBound(3, request.pageSize, 200);
    const int pageNo = qMax(1, request.page);
    QUrl url(QStringLiteral("https://pixabay.com/api/videos/"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("key"), key);
    if (!request.query.trimmed().isEmpty())
        q.addQueryItem(QStringLiteral("q"), request.query.trimmed().left(100));
    else
        q.addQueryItem(QStringLiteral("order"), QStringLiteral("popular"));
    q.addQueryItem(QStringLiteral("safesearch"), QStringLiteral("true"));
    q.addQueryItem(QStringLiteral("per_page"), QString::number(perPage));
    q.addQueryItem(QStringLiteral("page"), QString::number(pageNo));
    url.setQuery(q);
    return getJson(makeRequest(url), [callback, pageNo, perPage](const QJsonDocument &doc, const ProviderError &error) {
        if (!error.ok()) {
            callback({}, error);
            return;
        }
        callback(parse(doc, pageNo, perPage), ProviderError::none());
    });
}

} // namespace cadra::assets
