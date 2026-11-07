#pragma once

#include "app/online/library/AssetProvider.h"

#include <QDateTime>

#include <utility>

namespace cadra::assets {

// Cadra Open Library: stickers and animated title templates (Lottie) whose licences allow
// redistribution — Cadra's own CC0 designs plus permissively licensed samples. The index is a
// JSON file published in the Cadra repository (updatable without a release) with a copy bundled
// in the app, so the panels work offline. No account.
//
// Index format (asset-library/index.json):
//   { "version": 1, "items": [ { "id", "kind": "sticker"|"title", "title", "tags", "file",
//     "license", "licenseUrl", "author", "authorUrl", "page", "editableText": bool } ] }
// "file" is relative to the index URL.
class CadraLibraryProvider : public AssetProvider
{
public:
    explicit CadraLibraryProvider(ProviderContext *context, QUrl remoteIndex = defaultRemoteIndex());

    static QUrl defaultRemoteIndex();
    static QUrl bundledIndex() { return QUrl(QStringLiteral("qrc:/asset-library/index.json")); }

    ProviderInfo info() const override;
    RequestHandle search(const SearchRequest &request, SearchCallback callback) override;

    // Index → items (relative files resolved against `base`). Public for tests.
    static QList<AssetItem> parseIndex(const QJsonDocument &doc, const QUrl &base);
    static SearchPage filter(const QList<AssetItem> &all, const SearchRequest &request);

private:
    void ensureIndex(std::function<void()> then);
    void loadBundled();

    QUrl m_remoteIndex;
    QList<AssetItem> m_items;
    QDateTime m_loadedAt;
    QList<std::function<void()>> m_waiting;
};

} // namespace cadra::assets
