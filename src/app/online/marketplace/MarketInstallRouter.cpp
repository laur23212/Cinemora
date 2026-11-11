#include "app/online/marketplace/MarketInstallRouter.h"

#include "app/online/assets/DriftAssetStore.h"
#include "app/online/assets/SfxLibrary.h"
#include "app/online/downloads/DownloadCenter.h"
#include "app/online/marketplace/CommunityInstaller.h"
#include "app/online/marketplace/StockInstaller.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QUrl>

namespace {
const char kRemovedKey[] = "dafin/marketRemovedFromMedia";

QString mediaKey(const QString &path)
{
    const QString clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
    return clean.toLower();
#else
    return clean;
#endif
}
} // namespace

MarketInstallRouter::MarketInstallRouter(StockInstaller *stock, DriftAssetStore *assets, SfxLibrary *sfx,
                                         CommunityInstaller *community, DownloadCenter *center,
                                         QObject *parent)
    : QObject(parent)
    , m_stock(stock)
    , m_assets(assets)
    , m_sfx(sfx)
    , m_community(community)
    , m_center(center)
{
    const auto fromJob = [this](const QString &id) {
        const DownloadCenter::Job job = m_center->job(id);
        if (job.kind != QLatin1String("stock"))
            return;
        const int slash = id.indexOf(QLatin1Char('/'));
        bump(id.left(slash), id.mid(slash + 1));
    };
    if (m_center) {
        connect(m_center, &DownloadCenter::jobStarted, this, fromJob);
        connect(m_center, &DownloadCenter::jobFinished, this, fromJob);
    }
    if (m_assets) {
        connect(m_assets, &DriftAssetStore::stateChanged, this, [this](const QString &key) {
            bump(QStringLiteral("drift-assets"), key.section(QStringLiteral("--"), 0, 0));
        });
    }
    if (m_sfx) {
        connect(m_sfx, &SfxLibrary::stateChanged, this,
                [this](const QString &id) { bump(QStringLiteral("sfx"), id); });
    }
    if (m_community)
        connect(m_community, &CommunityInstaller::stateChanged, this, &MarketInstallRouter::bump);
    const QStringList removed = QSettings().value(QLatin1String(kRemovedKey)).toStringList();
    m_removedFromMedia = QSet<QString>(removed.cbegin(), removed.cend());
}

void MarketInstallRouter::setInMedia(const QString &path, bool inMedia)
{
    if (path.isEmpty())
        return;
    const QString key = mediaKey(path);
    if (inMedia ? !m_removedFromMedia.remove(key) : m_removedFromMedia.contains(key))
        return;
    if (!inMedia) {
        if (!isInstalledFile(key))
            return; // the user's own file, or another tab's download: none of the router's business
        m_removedFromMedia.insert(key);
    }
    saveRemoved();
    bump({}, {});
}

bool MarketInstallRouter::isInstalledFile(const QString &key) const
{
    for (const QString &root : {SfxLibrary::installRoot(), DriftAssetStore::installRoot()}) {
        if (!root.isEmpty() && key.startsWith(mediaKey(root) + QLatin1Char('/')))
            return true;
    }
    // Stock saves to a folder the user picks: match the finished stock jobs' files.
    if (m_center) {
        const QVariantList jobs = m_center->jobs();
        for (const QVariant &v : jobs) {
            const QVariantMap job = v.toMap();
            const QString file = job.value(QStringLiteral("filePath")).toString();
            if (job.value(QStringLiteral("kind")).toString() == QLatin1String("stock") && !file.isEmpty()
                && mediaKey(file) == key)
                return true;
        }
    }
    return false;
}

void MarketInstallRouter::saveRemoved() const
{
    // Files that are gone need no memory.
    QStringList keep;
    for (const QString &p : m_removedFromMedia)
        if (QFileInfo::exists(p))
            keep << p;
    QSettings().setValue(QLatin1String(kRemovedKey), keep);
}

bool MarketInstallRouter::removedFromMedia(const QVariantMap &result) const
{
    const QString path = result.value(QStringLiteral("path")).toString();
    return !path.isEmpty() && m_removedFromMedia.contains(mediaKey(path));
}

QString MarketInstallRouter::family(const QString &source)
{
    if (source.startsWith(QLatin1String("stock.")))
        return QStringLiteral("stock");
    if (source == QLatin1String("drift-assets") || source == QLatin1String("sfx"))
        return source;
    if (source.startsWith(QLatin1String("community.")))
        return QStringLiteral("community");
    return {};
}

void MarketInstallRouter::bump(const QString &source, const QString &itemId)
{
    ++m_revision;
    emit revisionChanged();
    emit stateChanged(source, itemId);
}

void MarketInstallRouter::install(const QVariantMap &item, const QString &variantId, const QString &destDir)
{
    const QString source = item.value(QStringLiteral("source")).toString();
    const QString kind = family(source);
    // Dafin: back into Media from the file already on disk (Drift Assets and sounds re-import an
    // existing file themselves; a stock download would fetch a second copy, so it's done here).
    const QVariantMap previous = rawResult(item, variantId);
    if (removedFromMedia(previous)) {
        const QString path = previous.value(QStringLiteral("path")).toString();
        m_removedFromMedia.remove(mediaKey(path));
        saveRemoved();
        if (kind == QLatin1String("stock") && QFileInfo::exists(path) && m_importer) {
            m_importer({path});
            bump(source, item.value(QStringLiteral("id")).toString());
            return;
        }
        bump(source, item.value(QStringLiteral("id")).toString());
    }
    if (kind == QLatin1String("stock") && m_stock) {
        m_stock->download(source, item.value(QStringLiteral("id")).toString(), variantId,
                          destDir.isEmpty() ? QUrl() : QUrl::fromLocalFile(destDir),
                          item.value(QStringLiteral("title")).toString(),
                          item.value(QStringLiteral("mediaKind")).toString());
    } else if (kind == QLatin1String("drift-assets") && m_assets) {
        m_assets->install(item, variantId);
    } else if (kind == QLatin1String("sfx") && m_sfx) {
        m_sfx->install(item);
    } else if (kind == QLatin1String("community") && m_community) {
        m_community->install(item);
    }
}

QString MarketInstallRouter::installState(const QVariantMap &item, const QString &variantId) const
{
    const QString state = rawState(item, variantId);
    if ((state == QLatin1String("installed") || state == QLatin1String("update"))
        && removedFromMedia(rawResult(item, variantId)))
        return QStringLiteral("none");
    return state;
}

QVariantMap MarketInstallRouter::installResult(const QVariantMap &item, const QString &variantId) const
{
    const QVariantMap result = rawResult(item, variantId);
    return removedFromMedia(result) ? QVariantMap() : result;
}

QString MarketInstallRouter::rawState(const QVariantMap &item, const QString &variantId) const
{
    const QString source = item.value(QStringLiteral("source")).toString();
    const QString kind = family(source);
    if (kind == QLatin1String("stock") && m_center) {
        const DownloadCenter::Job job = m_center->job(
            StockInstaller::jobId(source, item.value(QStringLiteral("id")).toString()));
        if (job.id.isEmpty() || job.status == QLatin1String("cancelled"))
            return QStringLiteral("none");
        if (job.status == QLatin1String("done"))
            return QStringLiteral("installed");
        return job.status == QLatin1String("failed") ? QStringLiteral("failed") : QStringLiteral("installing");
    }
    if (kind == QLatin1String("drift-assets") && m_assets)
        return m_assets->itemState(item, variantId);
    if (kind == QLatin1String("sfx") && m_sfx)
        return m_sfx->state(item.value(QStringLiteral("id")).toString());
    if (kind == QLatin1String("community") && m_community)
        return m_community->state(item);
    return QStringLiteral("none");
}

QVariantMap MarketInstallRouter::rawResult(const QVariantMap &item, const QString &variantId) const
{
    const QString source = item.value(QStringLiteral("source")).toString();
    const QString kind = family(source);
    if (kind == QLatin1String("stock") && m_center)
        return m_center->job(StockInstaller::jobId(source, item.value(QStringLiteral("id")).toString())).result;
    if (kind == QLatin1String("drift-assets") && m_assets) {
        const QString path = m_assets->localPath(m_assets->installKey(item, variantId));
        return path.isEmpty() ? QVariantMap() : QVariantMap{{QStringLiteral("path"), path}};
    }
    if (kind == QLatin1String("sfx") && m_sfx) {
        const QString path = m_sfx->localPath(item.value(QStringLiteral("id")).toString());
        return path.isEmpty() ? QVariantMap() : QVariantMap{{QStringLiteral("path"), path}};
    }
    if (kind == QLatin1String("community") && m_community)
        return m_community->result(item);
    return {};
}

QString MarketInstallRouter::installedPath(const QVariantMap &item, const QString &variantId) const
{
    return installResult(item, variantId).value(QStringLiteral("path")).toString();
}

#include "moc_MarketInstallRouter.cpp"
