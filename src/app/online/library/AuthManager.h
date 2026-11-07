#pragma once

#include "app/online/library/AssetProvider.h"

#include <QHash>
#include <QObject>
#include <QSet>

#include <memory>

namespace dafin::assets {

class ProviderRegistry;
class SecretStore;

// Owns provider sign-ins: stores credentials in the SecretStore, restores them at startup,
// tracks each provider's connection state and runs connect / OAuth / reconnect / logout.
// Credentials never reach QML; the UI only sees states, messages and which fields are set.
class AuthManager : public QObject
{
    Q_OBJECT
public:
    AuthManager(std::unique_ptr<SecretStore> store, QObject *parent = nullptr);
    ~AuthManager() override;

    void setRegistry(ProviderRegistry *registry) { m_registry = registry; }

    // Startup: sets every provider's state from what is stored (sessions restore silently).
    void restore();
    // One-time move of keys the old Stock Media tab kept in plain QSettings into the store.
    void migrateLegacyKeys();

    Credentials credentials(const QString &providerId) const;
    // Returns false when the secret store refused the write (the value is then kept in memory only).
    bool storeCredentials(const QString &providerId, const Credentials &credentials);
    void markExpired(const QString &providerId, const QString &message);

    ConnectionState state(const QString &providerId) const;
    QString message(const QString &providerId) const;
    bool isBusy(const QString &providerId) const { return m_busy.contains(providerId); }
    QStringList storedFieldIds(const QString &providerId) const;

    void connectProvider(const QString &providerId, const QVariantMap &input);
    // Saves the non-code fields (client id/secret) and returns the browser URL for sign-in.
    QUrl beginOAuth(const QString &providerId, const QVariantMap &input);
    void completeOAuth(const QString &providerId, const QString &code);
    void reconnect(const QString &providerId);
    void logout(const QString &providerId);

    QString storageName() const;
    bool storageEncrypted() const;

signals:
    void stateChanged(const QString &providerId);
    void authFinished(const QString &providerId, bool ok, const QString &message);

private:
    void runAuthenticate(const QString &providerId, const QVariantMap &input, const Credentials &mergeInto);
    void setState(const QString &providerId, ConnectionState state, const QString &message = {});
    ConnectionState idleState(const QString &providerId) const;
    static QString storeKey(const QString &providerId) { return QStringLiteral("asset-provider/") + providerId; }

    std::unique_ptr<SecretStore> m_store;
    ProviderRegistry *m_registry = nullptr;
    mutable QHash<QString, Credentials> m_cache;
    mutable QSet<QString> m_loaded;
    QHash<QString, ConnectionState> m_states;
    QHash<QString, QString> m_messages;
    QSet<QString> m_busy;
};

} // namespace dafin::assets
