#include "app/online/library/ProviderRegistry.h"

#include "app/online/library/AuthManager.h"
#include "app/online/library/providers/DafinLibraryProvider.h"
#include "app/online/library/providers/DafinSoundEffectsProvider.h"
#include "app/online/library/providers/CatalogProvider.h"
#include "app/online/library/providers/FreesoundProvider.h"
#include "app/online/library/providers/JamendoProvider.h"
#include "app/online/library/providers/OpenverseProvider.h"
#include "app/online/library/providers/PexelsProvider.h"
#include "app/online/library/providers/PixabayProvider.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace dafin::assets {

ProviderRegistry::ProviderRegistry(QNetworkAccessManager *network, AuthManager *auth, QObject *parent)
    : QObject(parent)
    , m_network(network)
    , m_auth(auth)
{
}

ProviderRegistry::~ProviderRegistry() = default;

void ProviderRegistry::registerBuiltins()
{
    // To add a provider: implement AssetProvider and add one line here. Order = display order.
    add(std::make_unique<DafinLibraryProvider>(this));
    add(std::make_unique<DafinSoundEffectsProvider>(this));
    add(std::make_unique<PexelsProvider>(this));
    add(std::make_unique<PixabayProvider>(this));
    add(std::make_unique<OpenverseProvider>(this));
    add(std::make_unique<JamendoProvider>(this));
    add(std::make_unique<FreesoundProvider>(this));
}

void ProviderRegistry::add(std::unique_ptr<AssetProvider> provider)
{
    const QString id = provider->info().id;
    m_providers.erase(std::remove_if(m_providers.begin(), m_providers.end(),
                                     [&id](const auto &p) { return p->info().id == id; }),
                      m_providers.end());
    m_providers.push_back(std::move(provider));
    emit providersChanged();
}

void ProviderRegistry::loadCatalogProviders(const QString &catalogPath, const QStringList &skipIds)
{
    m_providers.erase(std::remove_if(m_providers.begin(), m_providers.end(),
                                     [](const auto &p) { return p->info().id.startsWith(QLatin1String("catalog-")); }),
                      m_providers.end());
    QFile f(catalogPath);
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonArray sources = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("sources")).toArray();
        for (const QJsonValue &v : sources) {
            const QJsonObject s = v.toObject();
            const QString id = s.value(QStringLiteral("id")).toString();
            if (id.isEmpty() || skipIds.contains(id))
                continue;
            m_providers.push_back(std::make_unique<CatalogProvider>(this, s));
        }
    }
    emit providersChanged();
}

AssetProvider *ProviderRegistry::provider(const QString &id) const
{
    for (const auto &p : m_providers) {
        if (p->info().id == id)
            return p.get();
    }
    return nullptr;
}

QList<AssetProvider *> ProviderRegistry::providers() const
{
    QList<AssetProvider *> out;
    for (const auto &p : m_providers)
        out.append(p.get());
    return out;
}

QList<AssetProvider *> ProviderRegistry::providersFor(AssetKind kind) const
{
    QList<AssetProvider *> out;
    for (const auto &p : m_providers) {
        if (p->info().kinds.contains(kind))
            out.append(p.get());
    }
    return out;
}

Credentials ProviderRegistry::credentials(const QString &providerId) const
{
    return m_auth ? m_auth->credentials(providerId) : Credentials();
}

void ProviderRegistry::updateCredentials(const QString &providerId, const Credentials &credentials)
{
    if (m_auth)
        m_auth->storeCredentials(providerId, credentials);
}

void ProviderRegistry::reportAuthFailure(const QString &providerId, const ProviderError &error)
{
    if (m_auth)
        m_auth->markExpired(providerId, error.message);
}

QByteArray ProviderRegistry::userAgent() const
{
    const QString version = QCoreApplication::applicationVersion();
    return QStringLiteral("Dafin/%1 (+https://github.com/laur23212/Dafin)")
        .arg(version.isEmpty() ? QStringLiteral("dev") : version).toUtf8();
}

} // namespace dafin::assets
