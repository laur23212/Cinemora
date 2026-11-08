#include "app/online/library/providers/UserLibraryProvider.h"

#include <QFileInfo>

namespace dafin::assets {

UserLibraryProvider::UserLibraryProvider(ProviderContext *context, UserLibrary *library, QString collection)
    : AssetProvider(context)
    , m_library(library)
    , m_collection(std::move(collection))
{
}

ProviderInfo UserLibraryProvider::info() const
{
    const UserLibrary::Collection *c = UserLibrary::collection(m_collection);
    ProviderInfo i;
    i.id = c ? c->providerId : m_collection;
    i.name = UserLibrary::displayName(m_collection);
    if (c)
        i.kinds = {c->kind};
    i.description = QObject::tr("Files you added yourself. Drag files onto the panel to add more.");
    i.license = QObject::tr("Your own files");
    i.requiresAccount = false;
    i.responseCacheSeconds = 0; // a file added a moment ago must show up on the next search
    i.thumbnailCacheSeconds = 0;
    i.userLibrary = true;
    i.ownSidebarEntry = c ? c->ownSidebarEntry : true;
    return i;
}

AssetItem UserLibraryProvider::toItem(const UserLibrary::Collection &c, const UserLibrary::Entry &e)
{
    AssetItem a;
    a.providerId = c.providerId;
    a.id = e.id;
    a.kind = c.kind;
    a.title = e.title;
    const QString file = e.filePath();
    a.downloadUrl = file.isEmpty() ? e.link : QUrl::fromLocalFile(file);
    switch (c.kind) {
    case AssetKind::Sticker: {
        const QString preview = e.metaFile(QStringLiteral("preview"));
        a.previewUrl = preview.isEmpty() ? a.downloadUrl : QUrl::fromLocalFile(preview);
        break;
    }
    case AssetKind::Title: {
        const QString video = e.metaFile(QStringLiteral("previewVideo"));
        if (!video.isEmpty())
            a.previewUrl = QUrl::fromLocalFile(video);
        const QString poster = e.metaFile(QStringLiteral("poster"));
        if (!poster.isEmpty())
            a.thumbnailUrl = QUrl::fromLocalFile(poster);
        break;
    }
    default:
        a.previewUrl = a.downloadUrl;
        break;
    }
    a.durationSeconds = e.meta.value(QStringLiteral("duration")).toDouble();
    a.width = e.meta.value(QStringLiteral("width")).toInt();
    a.height = e.meta.value(QStringLiteral("height")).toInt();
    a.extra = e.meta;
    a.extra.insert(QStringLiteral("userLibrary"), true);
    a.extra.insert(QStringLiteral("collection"), c.id);
    a.extra.insert(QStringLiteral("saved"), e.saved);
    a.extra.insert(QStringLiteral("isLink"), file.isEmpty());
    if (c.kind == AssetKind::Title)
        a.extra.insert(QStringLiteral("format"), QStringLiteral("mogrt"));
    return a;
}

RequestHandle UserLibraryProvider::search(const SearchRequest &request, SearchCallback callback)
{
    const UserLibrary::Collection *c = UserLibrary::collection(m_collection);
    SearchPage page;
    if (!m_library || !c || request.kind != c->kind) {
        callback(page, ProviderError::none());
        return {};
    }
    const QStringList words = request.query.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QList<AssetItem> matching;
    for (const UserLibrary::Entry &e : m_library->entries(m_collection)) {
        QString hay = e.title + QLatin1Char(' ') + e.meta.value(QStringLiteral("originalName")).toString()
                      + QLatin1Char(' ') + e.meta.value(QStringLiteral("text")).toString();
        hay = hay.toLower();
        bool ok = true;
        for (const QString &w : words)
            ok = ok && hay.contains(w);
        if (ok)
            matching.append(toItem(*c, e));
    }
    const int size = qMax(1, request.pageSize);
    const int from = (qMax(1, request.page) - 1) * size;
    page.items = matching.mid(from, size);
    page.hasMore = from + size < matching.size();
    page.totalResults = int(matching.size());
    callback(page, ProviderError::none());
    return {};
}

RequestHandle UserLibraryProvider::download(const AssetItem &item, DownloadSpecCallback callback)
{
    if (item.kind == AssetKind::Title) {
        // Templates become titles on the timeline; there is no media file to add.
        callback({}, ProviderError::make(ProviderError::Code::Forbidden,
                                         QObject::tr("Click the template to add it to the timeline as a title.")));
        return {};
    }
    if (!item.downloadUrl.isLocalFile())
        return AssetProvider::download(item, std::move(callback)); // a remembered link
    const QString path = item.downloadUrl.toLocalFile();
    if (!QFileInfo::exists(path)) {
        callback({}, ProviderError::make(ProviderError::Code::NotFound,
                                         QObject::tr("This file is no longer in your library.")));
        return {};
    }
    DownloadSpec spec;
    spec.request = QNetworkRequest(item.downloadUrl);
    // The real extension: the shared helper only knows the common ones and would call an .opus .mp3.
    spec.suggestedFileName = util::safeFileName(item.title) + QStringLiteral(" [") + util::safeFileName(item.id)
                             + QStringLiteral("].") + QFileInfo(path).suffix().toLower();
    spec.expectedBytes = QFileInfo(path).size();
    callback(spec, ProviderError::none());
    return {};
}

} // namespace dafin::assets
