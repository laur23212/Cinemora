#pragma once

#include "app/online/library/AssetProvider.h"

namespace cadra::assets {

// Pexels stock video. Free API key (Authorization header). Terms: show a prominent link to
// Pexels, credit the videographer, do not replicate Pexels' core functionality.
// https://www.pexels.com/api/documentation/
class PexelsProvider : public AssetProvider
{
public:
    using AssetProvider::AssetProvider;

    ProviderInfo info() const override;
    RequestHandle search(const SearchRequest &request, SearchCallback callback) override;
    RequestHandle download(const AssetItem &item, DownloadSpecCallback callback) override;

    // Pure parsing, unit-tested without the network.
    static SearchPage parse(const QJsonDocument &doc, int pageSize);
};

} // namespace cadra::assets
