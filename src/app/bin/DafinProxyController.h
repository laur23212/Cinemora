#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

class AssetLibrary;

// Dafin proxy media settings for QML (singleton "DafinProxies"): the per-project switch, what to
// do after importing heavy clips, the encoder, the cache cap and "Clear proxy cache". Building,
// progress and the clip badges stay with AssetLibrary; the rules are in DafinProxyPolicy.
class DafinProxyController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool projectEnabled READ projectEnabled WRITE setProjectEnabled NOTIFY projectEnabledChanged)
    Q_PROPERTY(QString importChoice READ importChoice WRITE setImportChoice NOTIFY importChoiceChanged)
    Q_PROPERTY(QString encoder READ encoder WRITE setEncoder NOTIFY encoderChanged)
    Q_PROPERTY(QString lastEncoder READ lastEncoder NOTIFY cacheChanged)
    Q_PROPERTY(int cacheLimitGb READ cacheLimitGb WRITE setCacheLimitGb NOTIFY cacheChanged)
    Q_PROPERTY(QVariantList cacheLimitChoices READ cacheLimitChoices CONSTANT)
    Q_PROPERTY(QString cacheSizeText READ cacheSizeText NOTIFY cacheChanged)

public:
    explicit DafinProxyController(AssetLibrary *library, QObject *parent = nullptr);

    bool projectEnabled() const { return m_projectEnabled; }
    void setProjectEnabled(bool enabled);
    QString importChoice() const;
    void setImportChoice(const QString &choice);
    QString encoder() const;
    void setEncoder(const QString &encoder);
    QString lastEncoder() const;
    int cacheLimitGb() const;
    void setCacheLimitGb(int gb);
    QVariantList cacheLimitChoices() const;
    QString cacheSizeText() const { return m_cacheSizeText; }

    // For the import prompt: [{ id, name, reasons: "4K · HEVC · 10-bit" }] per asset.
    Q_INVOKABLE QVariantList describe(const QStringList &assetIds) const;
    // Re-reads the cache folder's size (Settings page shows it).
    Q_INVOKABLE void refreshCacheSize();
    // Stops proxy jobs, deletes every cached preview proxy and refreshes preview and badges.
    // Returns how many were deleted.
    Q_INVOKABLE int clearCache();

signals:
    void projectEnabledChanged();
    void importChoiceChanged();
    void encoderChanged();
    void cacheChanged();

private:
    QString projectId() const;
    void syncProject();
    void applyProjectFlag();
    void refreshPreviewAndBadges();
    void trimCache();

    AssetLibrary *m_library = nullptr;
    QString m_projectId;
    bool m_projectEnabled = true;
    QString m_cacheSizeText;
};
