#include "app/online/library/providers/DafinLibraryProvider.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSettings>

namespace dafin::assets {

DafinLibraryProvider::DafinLibraryProvider(ProviderContext *context, QUrl remoteIndex)
    : AssetProvider(context)
    , m_remoteIndex(std::move(remoteIndex))
{
}

QUrl DafinLibraryProvider::defaultRemoteIndex()
{
    // Overridable for testing a library branch: Settings key AssetProviders/dafinLibraryIndex.
    const QString custom = QSettings().value(QStringLiteral("AssetProviders/dafinLibraryIndex")).toString();
    if (!custom.isEmpty())
        return QUrl(custom);
    return QUrl(QStringLiteral("https://raw.githubusercontent.com/laur23212/Dafin/dafin-edition/asset-library/index.json"));
}

ProviderInfo DafinLibraryProvider::info() const
{
    ProviderInfo i;
    i.id = QStringLiteral("dafin");
    i.name = QStringLiteral("Dafin Open Library");
    i.kinds = {AssetKind::Sticker, AssetKind::Title};
    i.description = QObject::tr("Animated stickers and title templates that come with Dafin (CC0 and Apache-2.0).");
    i.license = QObject::tr("Per item (CC0 1.0 or Apache-2.0)");
    i.homepage = QUrl(QStringLiteral("https://github.com/laur23212/Dafin"));
    i.requiresAccount = false;
    i.responseCacheSeconds = 3600;
    return i;
}

QList<AssetItem> DafinLibraryProvider::parseIndex(const QJsonDocument &doc, const QUrl &base)
{
    QList<AssetItem> out;
    for (const QJsonValue &v : doc.object().value(QStringLiteral("items")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString file = o.value(QStringLiteral("file")).toString();
        bool ok = false;
        const AssetKind kind = kindFromString(o.value(QStringLiteral("kind")).toString(), &ok);
        if (file.isEmpty() || !ok || o.value(QStringLiteral("id")).toString().isEmpty())
            continue;
        AssetItem a;
        a.providerId = QStringLiteral("dafin");
        a.id = o.value(QStringLiteral("id")).toString();
        a.kind = kind;
        a.title = o.value(QStringLiteral("title")).toString(a.id);
        a.author = o.value(QStringLiteral("author")).toString(QStringLiteral("Dafin"));
        a.authorUrl = QUrl(o.value(QStringLiteral("authorUrl")).toString());
        a.pageUrl = QUrl(o.value(QStringLiteral("page")).toString());
        a.downloadUrl = base.resolved(QUrl(file));
        a.previewUrl = a.downloadUrl; // Lottie files are small: the preview is the file itself
        const QString thumb = o.value(QStringLiteral("thumbnail")).toString();
        if (!thumb.isEmpty())
            a.thumbnailUrl = base.resolved(QUrl(thumb));
        a.license = o.value(QStringLiteral("license")).toString(QStringLiteral("CC0 1.0"));
        a.licenseUrl = QUrl(o.value(QStringLiteral("licenseUrl")).toString());
        a.attribution = a.license.startsWith(QLatin1String("CC0"))
                            ? QString()
                            : QObject::tr("\"%1\" by %2, %3").arg(a.title, a.author, a.license);
        a.durationSeconds = o.value(QStringLiteral("duration")).toDouble();
        a.extra.insert(QStringLiteral("tags"), o.value(QStringLiteral("tags")).toString());
        a.extra.insert(QStringLiteral("editableText"), o.value(QStringLiteral("editableText")).toBool());
        out.append(a);
    }
    return out;
}

SearchPage DafinLibraryProvider::filter(const QList<AssetItem> &all, const SearchRequest &request)
{
    const QStringList words = request.query.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QList<AssetItem> matching;
    for (const AssetItem &a : all) {
        if (a.kind != request.kind)
            continue;
        const QString hay = (a.title + QLatin1Char(' ') + a.extra.value(QStringLiteral("tags")).toString()
                             + QLatin1Char(' ') + a.author).toLower();
        bool ok = true;
        for (const QString &w : words)
            ok = ok && hay.contains(w);
        if (ok)
            matching.append(a);
    }
    SearchPage page;
    const int size = qMax(1, request.pageSize);
    const int from = (qMax(1, request.page) - 1) * size;
    page.items = matching.mid(from, size);
    page.hasMore = from + size < matching.size();
    page.totalResults = int(matching.size());
    return page;
}

void DafinLibraryProvider::loadBundled()
{
    QFile f(QStringLiteral(":/asset-library/index.json"));
    if (f.open(QIODevice::ReadOnly))
        m_items = parseIndex(QJsonDocument::fromJson(f.readAll()), bundledIndex());
}

void DafinLibraryProvider::ensureIndex(std::function<void()> then)
{
    if (m_loadedAt.isValid() && m_loadedAt.secsTo(QDateTime::currentDateTimeUtc()) < 3600) {
        then();
        return;
    }
    m_waiting.append(std::move(then));
    if (m_waiting.size() > 1)
        return; // a fetch is already running
    if (m_items.isEmpty())
        loadBundled(); // offline-first: the bundled copy answers even if the fetch fails

    QNetworkReply *reply = context()->network()->get(makeRequest(m_remoteIndex));
    QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            const QList<AssetItem> remote = parseIndex(QJsonDocument::fromJson(reply->readAll()), m_remoteIndex);
            if (!remote.isEmpty())
                m_items = remote; // the published index supersedes the bundled one
        }
        m_loadedAt = QDateTime::currentDateTimeUtc();
        const auto waiting = std::exchange(m_waiting, {});
        for (const auto &fn : waiting)
            fn();
    });
}

RequestHandle DafinLibraryProvider::search(const SearchRequest &request, SearchCallback callback)
{
    RequestHandle handle;
    const auto cancelled = handle.flag();
    ensureIndex([this, request, callback, cancelled]() {
        if (*cancelled)
            return;
        callback(filter(m_items, request), ProviderError::none());
    });
    return handle;
}

} // namespace dafin::assets
