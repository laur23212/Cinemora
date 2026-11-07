#include "app/online/library/AuthManager.h"

#include "app/online/library/ProviderRegistry.h"
#include "app/online/library/SecretStore.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

namespace cadra::assets {

AuthManager::AuthManager(std::unique_ptr<SecretStore> store, QObject *parent)
    : QObject(parent)
    , m_store(std::move(store))
{
}

AuthManager::~AuthManager() = default;

QString AuthManager::storageName() const { return m_store ? m_store->backendName() : QString(); }
bool AuthManager::storageEncrypted() const { return m_store && m_store->isEncrypted(); }

Credentials AuthManager::credentials(const QString &providerId) const
{
    if (!m_loaded.contains(providerId)) {
        m_loaded.insert(providerId);
        if (m_store) {
            if (const auto raw = m_store->read(storeKey(providerId)))
                m_cache.insert(providerId, QJsonDocument::fromJson(*raw).object().toVariantMap());
        }
    }
    return m_cache.value(providerId);
}

void AuthManager::storeCredentials(const QString &providerId, const Credentials &credentials)
{
    m_loaded.insert(providerId);
    if (credentials.isEmpty()) {
        m_cache.remove(providerId);
        if (m_store)
            m_store->remove(storeKey(providerId));
        return;
    }
    m_cache.insert(providerId, credentials);
    if (m_store && !m_store->write(storeKey(providerId),
                                   QJsonDocument(QJsonObject::fromVariantMap(credentials)).toJson(QJsonDocument::Compact)))
        setState(providerId, ConnectionState::Error, tr("Could not save the sign-in securely. It works until Cadra closes."));
}

QStringList AuthManager::storedFieldIds(const QString &providerId) const
{
    QStringList out;
    const Credentials c = credentials(providerId);
    for (auto it = c.constBegin(); it != c.constEnd(); ++it) {
        if (!it.value().toString().isEmpty())
            out.append(it.key());
    }
    return out;
}

ConnectionState AuthManager::idleState(const QString &providerId) const
{
    AssetProvider *p = m_registry ? m_registry->provider(providerId) : nullptr;
    const bool stored = !credentials(providerId).isEmpty();
    if (stored)
        return ConnectionState::Connected;
    if (p && !p->info().requiresAccount && p->info().fields.isEmpty())
        return ConnectionState::NotRequired;
    if (p && !p->info().requiresAccount)
        return ConnectionState::NotRequired; // works anonymously; an account is optional
    return ConnectionState::NotConnected;
}

void AuthManager::restore()
{
    if (!m_registry)
        return;
    for (AssetProvider *p : m_registry->providers()) {
        const QString id = p->info().id;
        if (!m_states.contains(id) || m_states.value(id) != ConnectionState::Expired)
            setState(id, idleState(id));
    }
}

void AuthManager::migrateLegacyKeys()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("FreeLibrary/keys"));
    const QStringList keys = settings.childKeys();
    static const QHash<QString, QString> renamed{{QStringLiteral("pexels-video"), QStringLiteral("pexels")},
                                                 {QStringLiteral("pixabay-video"), QStringLiteral("pixabay")},
                                                 {QStringLiteral("freesound-sfx"), QStringLiteral("freesound")}};
    for (const QString &k : keys) {
        const QString value = settings.value(k).toString().trimmed();
        if (value.isEmpty())
            continue;
        const QString id = renamed.value(k, QStringLiteral("catalog-") + k);
        if (credentials(id).isEmpty())
            storeCredentials(id, {{QStringLiteral("apiKey"), value}});
    }
    settings.endGroup();
    // The plain-text copies go once they are in the secure store.
    settings.remove(QStringLiteral("FreeLibrary/keys"));
}

ConnectionState AuthManager::state(const QString &providerId) const
{
    return m_states.value(providerId, idleState(providerId));
}

QString AuthManager::message(const QString &providerId) const
{
    return m_messages.value(providerId);
}

void AuthManager::setState(const QString &providerId, ConnectionState state, const QString &message)
{
    m_states.insert(providerId, state);
    m_messages.insert(providerId, message);
    emit stateChanged(providerId);
}

void AuthManager::markExpired(const QString &providerId, const QString &message)
{
    // A probe during connect reports its own failure through authFinished; ignore it here.
    if (m_busy.contains(providerId))
        return;
    setState(providerId, ConnectionState::Expired, message);
}

void AuthManager::runAuthenticate(const QString &providerId, const QVariantMap &input, const Credentials &mergeInto)
{
    AssetProvider *p = m_registry ? m_registry->provider(providerId) : nullptr;
    if (!p) {
        emit authFinished(providerId, false, tr("Unknown provider"));
        return;
    }
    m_busy.insert(providerId);
    emit stateChanged(providerId);
    p->authenticate(input, [this, providerId, mergeInto](const Credentials &creds, const ProviderError &error) {
        m_busy.remove(providerId);
        if (!creds.isEmpty()) {
            Credentials merged = mergeInto;
            for (auto it = creds.constBegin(); it != creds.constEnd(); ++it)
                merged.insert(it.key(), it.value());
            storeCredentials(providerId, merged);
        }
        if (error.ok()) {
            setState(providerId, ConnectionState::Connected);
            emit authFinished(providerId, true, tr("Connected"));
            return;
        }
        // Stored but not usable yet (Openverse email verification) or a transient failure.
        const bool kept = !credentials(providerId).isEmpty();
        const ConnectionState s = error.code == ProviderError::Code::AuthExpired
                                      ? (kept ? ConnectionState::Expired : idleState(providerId))
                                  : kept ? ConnectionState::Error
                                         : idleState(providerId);
        setState(providerId, s, error.message);
        emit authFinished(providerId, false, error.message);
    });
}

void AuthManager::connectProvider(const QString &providerId, const QVariantMap &input)
{
    runAuthenticate(providerId, input, {});
}

QUrl AuthManager::beginOAuth(const QString &providerId, const QVariantMap &input)
{
    AssetProvider *p = m_registry ? m_registry->provider(providerId) : nullptr;
    if (!p)
        return {};
    // Keep what is already stored (API key, earlier tokens) and add what was typed.
    Credentials merged = credentials(providerId);
    for (auto it = input.constBegin(); it != input.constEnd(); ++it) {
        if (!it.value().toString().trimmed().isEmpty())
            merged.insert(it.key(), it.value().toString().trimmed());
    }
    storeCredentials(providerId, merged);
    return p->oauthAuthorizationUrl(merged);
}

void AuthManager::completeOAuth(const QString &providerId, const QString &code)
{
    runAuthenticate(providerId, {{QStringLiteral("code"), code.trimmed()}}, credentials(providerId));
}

void AuthManager::reconnect(const QString &providerId)
{
    const Credentials stored = credentials(providerId);
    if (stored.isEmpty()) {
        setState(providerId, idleState(providerId));
        return;
    }
    QVariantMap input = stored;
    input.remove(QStringLiteral("accessToken"));
    input.remove(QStringLiteral("expiresAt"));
    runAuthenticate(providerId, input, stored);
}

void AuthManager::logout(const QString &providerId)
{
    if (AssetProvider *p = m_registry ? m_registry->provider(providerId) : nullptr)
        p->logout();
    storeCredentials(providerId, {});
    setState(providerId, idleState(providerId));
}

} // namespace cadra::assets
