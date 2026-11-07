#pragma once

#include "app/online/library/AssetProvider.h"

namespace dafin::assets {

// Jamendo music (Creative Commons). Needs a free client_id from devportal.jamendo.com. Terms:
// offer download only when the track's `audiodownload_allowed` is true; credit artist + licence.
// https://developer.jamendo.com/v3.0
class JamendoProvider : public AssetProvider
{
public:
    using AssetProvider::AssetProvider;

    ProviderInfo info() const override;
    RequestHandle search(const SearchRequest &request, SearchCallback callback) override;

    static SearchPage parse(const QJsonDocument &doc, int pageSize, ProviderError *error);
};

} // namespace dafin::assets
