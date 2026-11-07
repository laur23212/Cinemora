#pragma once

#include "app/online/library/AssetProvider.h"

namespace cadra::assets {

// Pixabay stock video. Free API key (query parameter). Terms: show Pixabay as the source, cache
// search requests for 24 hours, no permanent hotlinking (files are downloaded before use).
// https://pixabay.com/api/docs/
class PixabayProvider : public AssetProvider
{
public:
    using AssetProvider::AssetProvider;

    ProviderInfo info() const override;
    RequestHandle search(const SearchRequest &request, SearchCallback callback) override;

    static SearchPage parse(const QJsonDocument &doc, int page, int pageSize);
};

} // namespace cadra::assets
