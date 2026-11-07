#include "app/online/library/LocalAssetDatabase.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace cadra::assets {

QJsonObject AssetRecord::toJson() const
{
    return {
        {QStringLiteral("key"), key},
        {QStringLiteral("providerId"), providerId},
        {QStringLiteral("assetId"), assetId},
        {QStringLiteral("kind"), kindToString(kind)},
        {QStringLiteral("title"), title},
        {QStringLiteral("author"), author},
        {QStringLiteral("license"), license},
        {QStringLiteral("licenseUrl"), licenseUrl},
        {QStringLiteral("attribution"), attribution},
        {QStringLiteral("pageUrl"), pageUrl},
        {QStringLiteral("localPath"), localPath},
        {QStringLiteral("bytes"), bytes},
        {QStringLiteral("storage"), storage},
        {QStringLiteral("downloadedAt"), downloadedAt.toString(Qt::ISODate)},
        {QStringLiteral("lastUsedAt"), lastUsedAt.toString(Qt::ISODate)},
    };
}

AssetRecord AssetRecord::fromJson(const QJsonObject &o)
{
    AssetRecord r;
    r.key = o.value(QStringLiteral("key")).toString();
    r.providerId = o.value(QStringLiteral("providerId")).toString();
    r.assetId = o.value(QStringLiteral("assetId")).toString();
    r.kind = kindFromString(o.value(QStringLiteral("kind")).toString());
    r.title = o.value(QStringLiteral("title")).toString();
    r.author = o.value(QStringLiteral("author")).toString();
    r.license = o.value(QStringLiteral("license")).toString();
    r.licenseUrl = o.value(QStringLiteral("licenseUrl")).toString();
    r.attribution = o.value(QStringLiteral("attribution")).toString();
    r.pageUrl = o.value(QStringLiteral("pageUrl")).toString();
    r.localPath = o.value(QStringLiteral("localPath")).toString();
    r.bytes = o.value(QStringLiteral("bytes")).toInteger();
    r.storage = o.value(QStringLiteral("storage")).toString(QStringLiteral("session"));
    r.downloadedAt = QDateTime::fromString(o.value(QStringLiteral("downloadedAt")).toString(), Qt::ISODate);
    r.lastUsedAt = QDateTime::fromString(o.value(QStringLiteral("lastUsedAt")).toString(), Qt::ISODate);
    return r;
}

LocalAssetDatabase::LocalAssetDatabase(QString path, QObject *parent)
    : QObject(parent)
    , m_path(std::move(path))
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, &LocalAssetDatabase::flush);
}

LocalAssetDatabase::~LocalAssetDatabase()
{
    if (m_saveTimer.isActive())
        flush();
}

void LocalAssetDatabase::load()
{
    m_records.clear();
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const int version = root.value(QStringLiteral("schemaVersion")).toInt(0);
    if (version > kSchemaVersion)
        return; // written by a newer Cadra: read nothing rather than lose fields on save
    // Migrations from older schema versions go here (version < kSchemaVersion).
    for (const QJsonValue &v : root.value(QStringLiteral("assets")).toArray()) {
        const AssetRecord r = AssetRecord::fromJson(v.toObject());
        if (!r.key.isEmpty())
            m_records.insert(r.key, r);
    }
}

int LocalAssetDatabase::reconcile()
{
    int removed = 0;
    for (auto it = m_records.begin(); it != m_records.end();) {
        if (!QFileInfo::exists(it->localPath)) {
            it = m_records.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    if (removed)
        scheduleSave();
    return removed;
}

std::optional<AssetRecord> LocalAssetDatabase::find(const QString &key) const
{
    const auto it = m_records.constFind(key);
    if (it == m_records.constEnd())
        return std::nullopt;
    return *it;
}

void LocalAssetDatabase::upsert(const AssetRecord &record)
{
    m_records.insert(record.key, record);
    scheduleSave();
    emit changed();
}

void LocalAssetDatabase::remove(const QString &key)
{
    if (m_records.remove(key)) {
        scheduleSave();
        emit changed();
    }
}

void LocalAssetDatabase::scheduleSave()
{
    m_saveTimer.start();
}

void LocalAssetDatabase::flush()
{
    m_saveTimer.stop();
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QJsonArray assets;
    for (const AssetRecord &r : std::as_const(m_records))
        assets.append(r.toJson());
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"), kSchemaVersion},
                                      {QStringLiteral("assets"), assets}}).toJson(QJsonDocument::Indented));
    f.commit();
}

} // namespace cadra::assets
