#include "app/bin/DafinProxyController.h"

#include "app/bin/AssetLibrary.h"
#include "core/project/Project.h"
#include "engine/decode/probe/MediaProbe.h"
#include "engine/encode/proxies/DafinProxyPolicy.h"
#include "engine/encode/proxies/ReverseProxyCache.h"

#include <QFileInfo>
#include <QVariantMap>
#include <QtConcurrent/QtConcurrentRun>

DafinProxyController::DafinProxyController(AssetLibrary *library, QObject *parent)
    : QObject(parent)
    , m_library(library)
{
    if (m_library) {
        // A project opened or a new one started: setProject resets the model.
        connect(m_library, &QAbstractItemModel::modelReset, this, &DafinProxyController::syncProject);
        connect(m_library, &AssetLibrary::countChanged, this, &DafinProxyController::syncProject);
        // A proxy finished: keep the cache under its cap and the size line current.
        connect(m_library, &AssetLibrary::proxiesChanged, this, &DafinProxyController::trimCache);
    }
    syncProject();
    refreshCacheSize();
}

QString DafinProxyController::projectId() const
{
    const drift::Project *project = m_library ? m_library->project() : nullptr;
    return project ? project->id() : QString();
}

void DafinProxyController::syncProject()
{
    const QString id = projectId();
    if (id == m_projectId && !id.isEmpty())
        return;
    m_projectId = id;
    const bool enabled = dafin::proxy::projectEnabled(id);
    const bool changed = enabled != m_projectEnabled;
    m_projectEnabled = enabled;
    applyProjectFlag();
    if (changed) {
        emit projectEnabledChanged();
        refreshPreviewAndBadges();
    }
}

void DafinProxyController::applyProjectFlag()
{
    drift::ReverseProxyCache::projectProxiesEnabled = m_projectEnabled;
}

void DafinProxyController::setProjectEnabled(bool enabled)
{
    if (enabled == m_projectEnabled)
        return;
    m_projectEnabled = enabled;
    dafin::proxy::setProjectEnabled(projectId(), enabled);
    applyProjectFlag();
    emit projectEnabledChanged();
    refreshPreviewAndBadges();
}

void DafinProxyController::refreshPreviewAndBadges()
{
    if (!m_library)
        return;
    // The preview re-reads (now with or without proxies) and the clip badges redraw.
    emit m_library->proxiesChanged();
    emit m_library->proxyStateChanged(QString());
}

QString DafinProxyController::importChoice() const
{
    return dafin::proxy::importChoice();
}

void DafinProxyController::setImportChoice(const QString &choice)
{
    if (choice == importChoice())
        return;
    dafin::proxy::setImportChoice(choice);
    emit importChoiceChanged();
}

QString DafinProxyController::encoder() const
{
    return dafin::proxy::encoderPreference();
}

void DafinProxyController::setEncoder(const QString &encoder)
{
    if (encoder == this->encoder())
        return;
    dafin::proxy::setEncoderPreference(encoder);
    // A newly chosen GPU gets a fresh chance even if it failed earlier this session.
    dafin::proxy::resetFailedEncoders();
    emit encoderChanged();
}

QString DafinProxyController::lastEncoder() const
{
    return dafin::proxy::lastUsedEncoder();
}

int DafinProxyController::cacheLimitGb() const
{
    return dafin::proxy::cacheLimitGb();
}

void DafinProxyController::setCacheLimitGb(int gb)
{
    if (gb == cacheLimitGb() || gb <= 0)
        return;
    dafin::proxy::setCacheLimitGb(gb);
    trimCache();
}

QVariantList DafinProxyController::cacheLimitChoices() const
{
    QVariantList choices;
    for (const int gb : dafin::proxy::cacheLimitChoicesGb())
        choices.append(gb);
    return choices;
}

QVariantList DafinProxyController::describe(const QStringList &assetIds) const
{
    QVariantList rows;
    const drift::Project *project = m_library ? m_library->project() : nullptr;
    for (const QString &id : assetIds) {
        const drift::MediaAsset *asset = project ? project->asset(id) : nullptr;
        if (!asset)
            continue;
        // Header-only probe: quick, and only for the few files of one import.
        const MediaInfo info = MediaProbe::probe(asset->path);
        QStringList reasons = dafin::proxy::heavyReasons(info, QFileInfo(asset->path).size());
        QVariantMap row;
        row.insert(QStringLiteral("id"), id);
        row.insert(QStringLiteral("name"), asset->name.isEmpty() ? QFileInfo(asset->path).fileName()
                                                                 : asset->name);
        row.insert(QStringLiteral("reasons"), reasons.join(QStringLiteral(" · ")));
        rows.append(row);
    }
    return rows;
}

void DafinProxyController::refreshCacheSize()
{
    m_cacheSizeText = dafin::proxy::formatBytes(dafin::proxy::cacheFolderBytes());
    emit cacheChanged();
}

int DafinProxyController::clearCache()
{
    if (m_library)
        m_library->cancelProxies();
    const int removed = drift::ReverseProxyCache::instance().clearPreviews();
    refreshPreviewAndBadges();
    refreshCacheSize();
    return removed;
}

void DafinProxyController::trimCache()
{
    const qint64 limit = dafin::proxy::cacheLimitBytes();
    // Off the UI thread: it stats every cached file and may delete several.
    QtConcurrent::run([limit] { drift::ReverseProxyCache::instance().trimToSize(limit); })
        .then(this, [this] { refreshCacheSize(); });
}
