#pragma once

#include "app/online/library/AssetProvider.h"

namespace dafin::assets {

// Freesound sound effects. An API key gives search and HQ MP3 previews (downloadable without
// OAuth); an optional OAuth2 sign-in (the user's own client id/secret) unlocks original-quality
// files. Terms: non-commercial use only; credit Freesound and each author per the sound's licence.
// https://freesound.org/docs/api/
class FreesoundProvider : public AssetProvider
{
public:
    using AssetProvider::AssetProvider;

    ProviderInfo info() const override;
    RequestHandle authenticate(const QVariantMap &input, AuthCallback callback) override;
    RequestHandle search(const SearchRequest &request, SearchCallback callback) override;
    RequestHandle download(const AssetItem &item, DownloadSpecCallback callback) override;
    QUrl oauthAuthorizationUrl(const Credentials &partial) const override;

    static SearchPage parse(const QJsonDocument &doc);
    static QString licenseName(const QString &licenseUrl);

private:
    // Exchanges an authorization code or a refresh token; merges the tokens into `base`.
    RequestHandle requestToken(const Credentials &base, const QList<QPair<QString, QString>> &form,
                               AuthCallback callback);
};

} // namespace dafin::assets
