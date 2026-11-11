#pragma once

#include "app/online/library/AssetProvider.h"

namespace dafin::assets {

// Dafin Sound Effects: the original sound-effects library made for Dafin (whooshes, impacts, UI
// clicks, notifications…), bundled in the app's resources so it works offline with no account.
// Built from a ChatGPT handoff by tools/sfx/build_sfx_index.py into asset-library/sound-effects/.
//
// The first search copies the Ogg files out of the resources into a cache folder named after the
// index's hash (<AppLocalData>/sound-effects/<hash>/<group>/<Title>.ogg; older folders are
// removed), so every item is a real file: the card previews it at once, drags it onto the timeline
// like a file from the file manager, and "+" adds it to Media through the usual download path.
//
// Index format (asset-library/sound-effects/index.json):
//   { "version": 1, "groups": [ {"id", "name", "subs": [{"id", "name", "count"}]} ],
//     "items": [ {"id", "title", "group", "sub", "file", "duration", "mood", "tags", "peaks"} ] }
// Groups are the rows under the source in the Audio panel, subs the chips above the grid.
class DafinSoundEffectsProvider : public AssetProvider
{
public:
    explicit DafinSoundEffectsProvider(ProviderContext *context,
                                       QString resourceRoot = QStringLiteral(":/sound-effects"),
                                       QString cacheRoot = defaultCacheRoot());

    static QString defaultCacheRoot();

    ProviderInfo info() const override;
    RequestHandle search(const SearchRequest &request, SearchCallback callback) override;

    // Index → items whose files live under `fileRoot`. Public for tests.
    static QList<AssetItem> parseIndex(const QJsonDocument &doc, const QString &fileRoot);
    static QVariantList parseGroups(const QJsonDocument &doc);
    static SearchPage filter(const QList<AssetItem> &all, const SearchRequest &request);
    // The extracted file of one index entry, relative to the cache folder: "<group>/<Title>.ogg".
    static QString cacheName(const QJsonObject &entry);

    // Copies every file into the cache (once per index version). False when one couldn't be
    // written; the items that were are still usable.
    bool ensureExtracted();
    QString cacheDir() const { return m_cacheDir; }

private:
    QString m_resourceRoot;
    QString m_cacheDir;
    QJsonDocument m_index;
    QList<AssetItem> m_items;
    QVariantList m_groups;
    bool m_extracted = false;
};

} // namespace dafin::assets
