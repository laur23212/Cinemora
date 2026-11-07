#pragma once

#include "app/online/library/AssetProvider.h"

namespace dafin::assets {

// Openverse (WordPress.org) openly licensed audio: music and sound effects aggregated from
// Jamendo, Freesound, ccMixter and others. Works anonymously; registering an application
// (client credentials) raises the rate limits. Terms: credit per the item's licence; no scraping.
// https://api.openverse.org/v1/
class OpenverseProvider : public AssetProvider
{
public:
    using AssetProvider::AssetProvider;

    ProviderInfo info() const override;
    RequestHandle authenticate(const QVariantMap &input, AuthCallback callback) override;
    void logout() override;
    RequestHandle search(const SearchRequest &request, SearchCallback callback) override;
    RequestHandle getAssetDetails(const AssetItem &item, DetailsCallback callback) override;

    static SearchPage parse(const QJsonDocument &doc, AssetKind kind);

private:
    // Runs `then` with a valid bearer token (fetching/refreshing one when credentials exist), or
    // with an empty token for anonymous access.
    RequestHandle withToken(std::function<void(const QString &token, const ProviderError &)> then);
    RequestHandle fetchToken(const Credentials &creds, AuthCallback callback);
};

} // namespace dafin::assets
