#pragma once

#include "app/online/library/AssetTypes.h"

#include <QHash>
#include <QObject>
#include <QDateTime>
#include <QJsonObject>
#include <QTimer>

#include <optional>

namespace dafin::assets {

// Every asset downloaded through a provider, with what is needed to credit it later. Stored as
// a versioned JSON document (written atomically, debounced) — see the design doc for the schema
// and why it is not SQLite yet.
struct AssetRecord
{
    QString key;            // providerId:id
    QString providerId;
    QString assetId;
    AssetKind kind = AssetKind::Video;
    QString title;
    QString author;
    QString license;
    QString licenseUrl;
    QString attribution;
    QString pageUrl;
    QString localPath;
    qint64 bytes = 0;
    QString storage;        // "session" (deleted on exit) or "library" (kept)
    QDateTime downloadedAt;
    QDateTime lastUsedAt;
    // Dafin: the user removed this file from Media, so its card offers "download" again (the
    // file stays on disk, and downloading it again re-imports that same file).
    bool removedFromMedia = false;

    QJsonObject toJson() const;
    static AssetRecord fromJson(const QJsonObject &o);
};

class LocalAssetDatabase : public QObject
{
    Q_OBJECT
public:
    static constexpr int kSchemaVersion = 1;

    explicit LocalAssetDatabase(QString path, QObject *parent = nullptr);
    ~LocalAssetDatabase() override;

    void load();
    // Drops records whose file is gone (session files after a restart). Returns how many.
    int reconcile();

    std::optional<AssetRecord> find(const QString &key) const;
    QList<AssetRecord> all() const { return m_records.values(); }
    void upsert(const AssetRecord &record);
    void remove(const QString &key);
    // Dafin: marks every record whose file is `path` as removed from (or back in) Media.
    // Returns the keys that changed.
    QStringList setRemovedFromMedia(const QString &path, bool removed);
    void flush();

    QString path() const { return m_path; }

signals:
    void changed();

private:
    void scheduleSave();
    QString m_path;
    QHash<QString, AssetRecord> m_records;
    QTimer m_saveTimer;
};

} // namespace dafin::assets
