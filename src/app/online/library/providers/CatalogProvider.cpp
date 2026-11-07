#include "app/online/library/providers/CatalogProvider.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrlQuery>
#include <QFileInfo>

namespace cadra::assets {

CatalogProvider::CatalogProvider(ProviderContext *context, QJsonObject source)
    : AssetProvider(context)
    , m_source(std::move(source))
    , m_id(QStringLiteral("catalog-") + m_source.value(QStringLiteral("id")).toString())
{
}

AssetKind CatalogProvider::kindOf(const QJsonObject &source)
{
    const QString k = source.value(QStringLiteral("kind")).toString().toLower();
    if (k == QLatin1String("image") || k == QLatin1String("lottie") || k == QLatin1String("sticker"))
        return AssetKind::Sticker;
    if (k == QLatin1String("music"))
        return AssetKind::Music;
    if (k == QLatin1String("audio") || k == QLatin1String("sfx"))
        return AssetKind::SoundEffect;
    if (k == QLatin1String("title"))
        return AssetKind::Title;
    return AssetKind::Video;
}

ProviderInfo CatalogProvider::info() const
{
    ProviderInfo i;
    i.id = m_id;
    i.name = m_source.value(QStringLiteral("name")).toString(m_id);
    i.kinds = {kindOf(m_source)};
    i.description = QObject::tr("From your catalog file.");
    i.license = m_source.value(QStringLiteral("license")).toString();
    i.homepage = QUrl(m_source.value(QStringLiteral("homepage")).toString());
    i.signupUrl = QUrl(m_source.value(QStringLiteral("keyUrl")).toString());
    i.requiresAccount = m_source.value(QStringLiteral("needsKey")).toBool();
    if (i.requiresAccount)
        i.fields = {{QStringLiteral("apiKey"), QObject::tr("API key"), true, false, QString()}};
    i.responseCacheSeconds = m_source.value(QStringLiteral("cacheSeconds")).toInt(24 * 3600);
    return i;
}

QJsonValue CatalogProvider::lookup(const QJsonValue &root, const QString &path)
{
    if (path.startsWith('\'') && path.endsWith('\'') && path.size() >= 2)
        return QJsonValue(path.mid(1, path.size() - 2));

    static const QRegularExpression pick(QStringLiteral("^(.*)\\{\\s*([A-Za-z0-9_]+)\\s*(<=|>=)\\s*([0-9.]+)\\s*\\}$"));
    QJsonValue cur = root;
    for (const QString &rawPart : path.split('.', Qt::SkipEmptyParts)) {
        QString part = rawPart;
        const QRegularExpressionMatch m = pick.match(part);
        QString field;
        QString op;
        double limit = 0;
        if (m.hasMatch()) {
            part = m.captured(1);
            field = m.captured(2);
            op = m.captured(3);
            limit = m.captured(4).toDouble();
        }
        if (!part.isEmpty()) {
            if (cur.isArray()) {
                bool isIndex = false;
                const int index = part.toInt(&isIndex);
                cur = isIndex ? cur.toArray().at(index) : QJsonValue();
            } else {
                cur = cur.toObject().value(part);
            }
        }
        if (!field.isEmpty()) {
            const QJsonArray array = cur.toArray();
            const bool atMost = op == QLatin1String("<=");
            QJsonValue best;
            QJsonValue fallback;
            bool haveBest = false;
            bool haveFallback = false;
            double bestValue = 0;
            double fallbackValue = 0;
            for (const QJsonValue &element : array) {
                const double value = element.toObject().value(field).toDouble();
                const bool inRange = atMost ? value <= limit : value >= limit;
                if (inRange && (!haveBest || (atMost ? value > bestValue : value < bestValue))) {
                    best = element;
                    bestValue = value;
                    haveBest = true;
                }
                // Nothing in range: take the one closest to the limit from the other side.
                if (!haveFallback || (atMost ? value < fallbackValue : value > fallbackValue)) {
                    fallback = element;
                    fallbackValue = value;
                    haveFallback = true;
                }
            }
            cur = haveBest ? best : fallback;
        }
        if (cur.isUndefined() || cur.isNull())
            return {};
    }
    return cur;
}

QString CatalogProvider::expand(const QString &tmpl, const QString &query, int page) const
{
    QString out = tmpl;
    out.replace(QStringLiteral("{query}"), QString::fromUtf8(QUrl::toPercentEncoding(query)));
    out.replace(QStringLiteral("{page}"), QString::number(page));
    out.replace(QStringLiteral("{perPage}"), QString::number(m_source.value(QStringLiteral("perPage")).toInt(30)));
    QString key = credentials().value(QStringLiteral("apiKey")).toString();
    if (key.isEmpty())
        key = m_source.value(QStringLiteral("apiKey")).toString();
    out.replace(QStringLiteral("{apiKey}"), key);
    return out;
}

QList<AssetItem> CatalogProvider::mapResults(const QJsonObject &source, const QString &providerId, const QJsonDocument &doc)
{
    QList<AssetItem> out;
    const QJsonValue root = doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
    const QJsonArray items = lookup(root, source.value(QStringLiteral("items")).toString()).toArray();
    const QJsonObject map = source.value(QStringLiteral("map")).toObject();
    auto field = [&map](const QJsonValue &entry, const char *name) {
        const QString path = map.value(QLatin1String(name)).toString();
        return path.isEmpty() ? QJsonValue() : lookup(entry, path);
    };
    for (const QJsonValue &entry : items) {
        AssetItem a;
        a.providerId = providerId;
        a.kind = kindOf(source);
        a.downloadUrl = QUrl(field(entry, "download").toVariant().toString());
        if (!a.downloadUrl.isValid())
            continue;
        a.id = field(entry, "id").toVariant().toString();
        if (a.id.isEmpty())
            a.id = QString::fromLatin1(QCryptographicHash::hash(a.downloadUrl.toEncoded(), QCryptographicHash::Sha1).toHex().left(12));
        a.title = util::prettyTitle(field(entry, "title").toVariant().toString());
        a.author = field(entry, "author").toVariant().toString();
        a.thumbnailUrl = QUrl(field(entry, "thumb").toVariant().toString());
        a.previewUrl = QUrl(field(entry, "preview").toVariant().toString());
        a.pageUrl = QUrl(field(entry, "page").toVariant().toString());
        a.width = field(entry, "width").toInt();
        a.height = field(entry, "height").toInt();
        a.durationSeconds = field(entry, "duration").toDouble();
        a.license = field(entry, "license").toVariant().toString();
        if (a.license.isEmpty())
            a.license = source.value(QStringLiteral("license")).toString();
        out.append(a);
    }
    return out;
}

RequestHandle CatalogProvider::search(const SearchRequest &request, SearchCallback callback)
{
    const QString type = m_source.value(QStringLiteral("type")).toString();
    const QString query = request.query.trimmed();
    if (type == QLatin1String("static")) {
        const QStringList words = query.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        SearchPage page;
        if (request.page <= 1) {
            for (const QJsonValue &v : m_source.value(QStringLiteral("items")).toArray()) {
                const QJsonObject o = v.toObject();
                const QString hay = (o.value(QStringLiteral("title")).toString() + QLatin1Char(' ')
                                     + o.value(QStringLiteral("tags")).toString()).toLower();
                bool ok = true;
                for (const QString &w : words)
                    ok = ok && hay.contains(w);
                if (!ok || o.value(QStringLiteral("url")).toString().isEmpty())
                    continue;
                AssetItem a;
                a.providerId = m_id;
                a.kind = kindOf(m_source);
                a.downloadUrl = QUrl(o.value(QStringLiteral("url")).toString());
                a.previewUrl = QUrl(o.value(QStringLiteral("preview")).toString(a.downloadUrl.toString()));
                a.thumbnailUrl = QUrl(o.value(QStringLiteral("thumb")).toString());
                a.id = QString::fromLatin1(QCryptographicHash::hash(a.downloadUrl.toEncoded(), QCryptographicHash::Sha1).toHex().left(12));
                a.title = o.value(QStringLiteral("title")).toString(QFileInfo(a.downloadUrl.path()).completeBaseName());
                a.author = o.value(QStringLiteral("author")).toString();
                a.license = o.value(QStringLiteral("license")).toString(m_source.value(QStringLiteral("license")).toString());
                a.pageUrl = QUrl(o.value(QStringLiteral("page")).toString());
                page.items.append(a);
            }
        }
        callback(page, ProviderError::none());
        return {};
    }
    const QString tmpl = m_source.value(query.isEmpty() ? QStringLiteral("browse") : QStringLiteral("search")).toString();
    if (tmpl.isEmpty()) {
        callback({}, ProviderError::make(ProviderError::Code::BadResponse,
                                         QObject::tr("This catalog source has no %1 URL.").arg(query.isEmpty() ? "browse" : "search")));
        return {};
    }
    if (info().requiresAccount && expand(QStringLiteral("{apiKey}"), {}, 1).isEmpty()) {
        callback({}, ProviderError::make(ProviderError::Code::AuthRequired,
                                         QObject::tr("Add the API key for %1 in Settings.").arg(info().name)));
        return {};
    }
    const int page = qMax(1, request.page);
    QNetworkRequest r = makeRequest(QUrl(expand(tmpl, query, page)));
    const QJsonObject headers = m_source.value(QStringLiteral("headers")).toObject();
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it)
        r.setRawHeader(it.key().toUtf8(), expand(it.value().toString(), query, page).toUtf8());
    const int perPage = m_source.value(QStringLiteral("perPage")).toInt(30);
    const QJsonObject source = m_source;
    const QString id = m_id;
    return getJson(r, [callback, source, id, perPage](const QJsonDocument &doc, const ProviderError &error) {
        if (!error.ok()) {
            callback({}, error);
            return;
        }
        SearchPage p;
        p.items = mapResults(source, id, doc);
        p.hasMore = p.items.size() >= perPage;
        callback(p, ProviderError::none());
    });
}

} // namespace cadra::assets
