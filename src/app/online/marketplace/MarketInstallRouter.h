#pragma once

#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QVariantMap>

#include <functional>

class CommunityInstaller;
class DownloadCenter;
class DriftAssetStore;
class SfxLibrary;
class StockInstaller;

// One entry point for installing a marketplace item, whatever its source. The source id's
// prefix picks the installer: stock.<type>, drift-assets, sfx or community.<kind>. QML asks
// this for "install" and "what state is this item in" and never needs to know which of the
// four installers is behind it.
class MarketInstallRouter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    MarketInstallRouter(StockInstaller *stock, DriftAssetStore *assets, SfxLibrary *sfx,
                        CommunityInstaller *community, DownloadCenter *center,
                        QObject *parent = nullptr);

    int revision() const { return m_revision; }

    // The source's family: stock, drift-assets, sfx or community. Empty when unknown.
    static QString family(const QString &source);

    // destDir only applies to stock, which saves to a folder the user picks (empty: app data).
    // A Drift Assets item installs its design variantId; the others ignore it unless the
    // source offers variants (stock resolutions).
    Q_INVOKABLE void install(const QVariantMap &item, const QString &variantId = QString(),
                             const QString &destDir = QString());
    // none | installing | installed | update | failed
    Q_INVOKABLE QString installState(const QVariantMap &item, const QString &variantId = QString()) const;
    // path, assetId and anything else the installer recorded; empty when not installed.
    Q_INVOKABLE QVariantMap installResult(const QVariantMap &item, const QString &variantId = QString()) const;
    Q_INVOKABLE QString installedPath(const QVariantMap &item, const QString &variantId = QString()) const;

    // Dafin: the media bin reports files leaving it (and coming back). An installed item whose
    // file left Media reads "none" again, so its card offers the download; installing it again
    // re-imports the file already on disk. Remembered across restarts.
    using Importer = std::function<QStringList(const QStringList &)>;
    void setImporter(Importer importer) { m_importer = std::move(importer); }
    void setInMedia(const QString &path, bool inMedia);

signals:
    void revisionChanged();
    void stateChanged(const QString &source, const QString &itemId);

private:
    void bump(const QString &source, const QString &itemId);
    QString rawState(const QVariantMap &item, const QString &variantId) const;
    QVariantMap rawResult(const QVariantMap &item, const QString &variantId) const;
    bool removedFromMedia(const QVariantMap &result) const;
    void saveRemoved() const;
    bool isInstalledFile(const QString &key) const;

    QPointer<StockInstaller> m_stock;
    QPointer<DriftAssetStore> m_assets;
    QPointer<SfxLibrary> m_sfx;
    QPointer<CommunityInstaller> m_community;
    QPointer<DownloadCenter> m_center;
    int m_revision = 0;
    Importer m_importer;
    QSet<QString> m_removedFromMedia; // cleaned absolute paths
};
