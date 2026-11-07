#pragma once

#include "app/online/library/AssetProvider.h"

#include <QObject>

#include <memory>
#include <vector>

class QNetworkAccessManager;

namespace cadra::assets {

class AuthManager;

// Owns every provider and is the ProviderContext they talk to. Built-in providers are listed in
// registerBuiltins(); catalog providers are rebuilt from the user's catalog file on reload.
class ProviderRegistry : public QObject, public ProviderContext
{
    Q_OBJECT
public:
    ProviderRegistry(QNetworkAccessManager *network, AuthManager *auth, QObject *parent = nullptr);
    ~ProviderRegistry() override;

    void registerBuiltins();
    void add(std::unique_ptr<AssetProvider> provider);
    // Replaces all "catalog-*" providers with the sources in the given catalog JSON.
    void loadCatalogProviders(const QString &catalogPath, const QStringList &skipIds);

    AssetProvider *provider(const QString &id) const;
    QList<AssetProvider *> providers() const;
    QList<AssetProvider *> providersFor(AssetKind kind) const;

    // ProviderContext
    QNetworkAccessManager *network() override { return m_network; }
    Credentials credentials(const QString &providerId) const override;
    void updateCredentials(const QString &providerId, const Credentials &credentials) override;
    void reportAuthFailure(const QString &providerId, const ProviderError &error) override;
    QByteArray userAgent() const override;

signals:
    void providersChanged();

private:
    QNetworkAccessManager *m_network;
    AuthManager *m_auth;
    std::vector<std::unique_ptr<AssetProvider>> m_providers;
};

} // namespace cadra::assets
