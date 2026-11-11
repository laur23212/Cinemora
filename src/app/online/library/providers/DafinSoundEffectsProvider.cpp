#include "app/online/library/providers/DafinSoundEffectsProvider.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>

#include <QSet>

#include <utility>

namespace dafin::assets {

namespace {
const QString kProviderId = QStringLiteral("dafin-sfx");
}

DafinSoundEffectsProvider::DafinSoundEffectsProvider(ProviderContext *context, QString resourceRoot, QString cacheRoot)
    : AssetProvider(context)
    , m_resourceRoot(std::move(resourceRoot))
{
    QFile f(m_resourceRoot + QStringLiteral("/index.json"));
    QByteArray bytes;
    if (f.open(QIODevice::ReadOnly))
        bytes = f.readAll();
    m_index = QJsonDocument::fromJson(bytes);
    // A new library (any change to the index) extracts into a folder of its own.
    const QString version = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex().left(12));
    m_cacheDir = QDir(cacheRoot).filePath(version);
    m_items = parseIndex(m_index, m_cacheDir);
    m_groups = parseGroups(m_index);
}

QString DafinSoundEffectsProvider::defaultCacheRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/sound-effects");
}

ProviderInfo DafinSoundEffectsProvider::info() const
{
    ProviderInfo i;
    i.id = kProviderId;
    i.name = QObject::tr("Dafin Sound Effects");
    i.kinds = {AssetKind::SoundEffect};
    i.description = QObject::tr("Whooshes, impacts, UI clicks, notifications and more, made for Dafin. Built in, works offline.");
    i.license = QObject::tr("Royalty-free originals, no attribution needed");
    i.homepage = QUrl(QStringLiteral("https://github.com/laur23212/Dafin"));
    i.requiresAccount = false;
    i.responseCacheSeconds = 0; // answered from memory
    i.categories = m_groups;
    return i;
}

QString DafinSoundEffectsProvider::cacheName(const QJsonObject &entry)
{
    const QString file = entry.value(QStringLiteral("file")).toString();
    const QString title = entry.value(QStringLiteral("title")).toString();
    const QString group = entry.value(QStringLiteral("group")).toString();
    const QString ext = QFileInfo(file).suffix().isEmpty() ? QStringLiteral("ogg") : QFileInfo(file).suffix();
    // Named after the effect, so Media lists "Soft Air Whoosh" rather than a file id.
    return util::safeFileName(group.isEmpty() ? QStringLiteral("other") : group) + QLatin1Char('/')
         + util::safeFileName(title.isEmpty() ? QFileInfo(file).completeBaseName() : title) + QLatin1Char('.') + ext;
}

QVariantList DafinSoundEffectsProvider::parseGroups(const QJsonDocument &doc)
{
    QVariantList out;
    for (const QJsonValue &g : doc.object().value(QStringLiteral("groups")).toArray()) {
        const QJsonObject o = g.toObject();
        QVariantList subs;
        int total = 0;
        for (const QJsonValue &s : o.value(QStringLiteral("subs")).toArray()) {
            const QJsonObject so = s.toObject();
            const int count = so.value(QStringLiteral("count")).toInt();
            total += count;
            subs.append(QVariantMap{{QStringLiteral("id"), so.value(QStringLiteral("id")).toString()},
                                    {QStringLiteral("name"), so.value(QStringLiteral("name")).toString()},
                                    {QStringLiteral("count"), count}});
        }
        out.append(QVariantMap{{QStringLiteral("id"), o.value(QStringLiteral("id")).toString()},
                               {QStringLiteral("name"), o.value(QStringLiteral("name")).toString()},
                               {QStringLiteral("count"), total},
                               {QStringLiteral("subs"), subs}});
    }
    return out;
}

QList<AssetItem> DafinSoundEffectsProvider::parseIndex(const QJsonDocument &doc, const QString &fileRoot)
{
    const QJsonObject root = doc.object();
    // Names of groups and subs, for the card's caption and for search.
    QHash<QString, QString> names;
    for (const QJsonValue &g : root.value(QStringLiteral("groups")).toArray()) {
        names.insert(g[QStringLiteral("id")].toString(), g[QStringLiteral("name")].toString());
        for (const QJsonValue &s : g[QStringLiteral("subs")].toArray())
            names.insert(s[QStringLiteral("id")].toString(), s[QStringLiteral("name")].toString());
    }
    const QString author = root.value(QStringLiteral("author")).toString(QStringLiteral("Dafin"));
    const QString license = root.value(QStringLiteral("license")).toString(QStringLiteral("Royalty-free"));

    QList<AssetItem> out;
    QSet<QString> seen;
    for (const QJsonValue &v : root.value(QStringLiteral("items")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString id = o.value(QStringLiteral("id")).toString();
        if (id.isEmpty() || o.value(QStringLiteral("file")).toString().isEmpty() || seen.contains(id))
            continue;
        seen.insert(id);
        const QString group = o.value(QStringLiteral("group")).toString();
        const QString sub = o.value(QStringLiteral("sub")).toString();
        AssetItem a;
        a.providerId = kProviderId;
        a.id = id;
        a.kind = AssetKind::SoundEffect;
        a.title = o.value(QStringLiteral("title")).toString(id);
        a.author = author;
        a.license = license;
        a.pageUrl = QUrl(QStringLiteral("https://github.com/laur23212/Dafin"));
        a.durationSeconds = o.value(QStringLiteral("duration")).toDouble();
        a.downloadUrl = QUrl::fromLocalFile(QDir(fileRoot).filePath(cacheName(o)));
        a.previewUrl = a.downloadUrl;
        QVariantList peaks;
        for (const QJsonValue &p : o.value(QStringLiteral("peaks")).toArray())
            peaks.append(p.toDouble());
        a.extra.insert(QStringLiteral("peaks"), peaks);
        a.extra.insert(QStringLiteral("group"), group);
        a.extra.insert(QStringLiteral("sub"), sub);
        a.extra.insert(QStringLiteral("mood"), o.value(QStringLiteral("mood")).toString());
        const QString subName = names.value(sub);
        a.extra.insert(QStringLiteral("caption"), subName);
        a.extra.insert(QStringLiteral("tags"), o.value(QStringLiteral("tags")).toString() + QLatin1Char(' ')
                                                   + names.value(group) + QLatin1Char(' ') + subName);
        out.append(a);
    }
    return out;
}

SearchPage DafinSoundEffectsProvider::filter(const QList<AssetItem> &all, const SearchRequest &request)
{
    const QStringList words = request.query.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QString group = request.category.section(QLatin1Char('/'), 0, 0);
    const QString sub = request.category.section(QLatin1Char('/'), 1, 1);
    QList<AssetItem> inTitle, inTags;
    for (const AssetItem &a : all) {
        if (a.kind != request.kind)
            continue;
        if (!group.isEmpty() && a.extra.value(QStringLiteral("group")).toString() != group)
            continue;
        if (!sub.isEmpty() && a.extra.value(QStringLiteral("sub")).toString() != sub)
            continue;
        const QString title = a.title.toLower();
        const QString hay = title + QLatin1Char(' ') + a.extra.value(QStringLiteral("tags")).toString().toLower();
        bool all_ = true, titleOnly = true;
        for (const QString &w : words) {
            all_ = all_ && hay.contains(w);
            titleOnly = titleOnly && title.contains(w);
        }
        if (!all_)
            continue;
        // "bell" finds Bell Ding before a chime that is only tagged bell.
        (titleOnly ? inTitle : inTags).append(a);
    }
    const QList<AssetItem> matching = inTitle + inTags;
    SearchPage page;
    const int size = qMax(1, request.pageSize);
    const int from = (qMax(1, request.page) - 1) * size;
    page.items = matching.mid(from, size);
    page.hasMore = from + size < matching.size();
    page.totalResults = int(matching.size());
    return page;
}

bool DafinSoundEffectsProvider::ensureExtracted()
{
    if (m_extracted)
        return true;
    bool ok = true;
    for (const QJsonValue &v : m_index.object().value(QStringLiteral("items")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString src = m_resourceRoot + QLatin1Char('/') + o.value(QStringLiteral("file")).toString();
        const QString dst = QDir(m_cacheDir).filePath(cacheName(o));
        const QFileInfo have(dst);
        if (have.exists() && have.size() == QFileInfo(src).size())
            continue;
        QDir().mkpath(have.absolutePath());
        QFile::remove(dst);
        if (QFile::copy(src, dst))
            QFile::setPermissions(dst, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
        else
            ok = false;
    }
    // Copies of earlier versions of the library.
    QDir root(QFileInfo(m_cacheDir).absolutePath());
    const QString current = QFileInfo(m_cacheDir).fileName();
    for (const QString &name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (name != current)
            QDir(root.filePath(name)).removeRecursively();
    }
    m_extracted = ok;
    return ok;
}

RequestHandle DafinSoundEffectsProvider::search(const SearchRequest &request, SearchCallback callback)
{
    RequestHandle handle;
    if (m_items.isEmpty()) {
        callback(SearchPage{}, ProviderError::none());
        return handle;
    }
    ensureExtracted();
    callback(filter(m_items, request), ProviderError::none());
    return handle;
}

} // namespace dafin::assets
