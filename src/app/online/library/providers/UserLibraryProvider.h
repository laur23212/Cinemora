#pragma once

#include "app/online/library/AssetProvider.h"
#include "app/online/library/UserLibrary.h"

#include <QPointer>

namespace dafin::assets {

// One collection of the user's own files (My Lotties, My sounds, imported titles) as an asset
// source, so it shows in the panels exactly like an online one: search, cards, previews and
// "Add to Media". Answers synchronously from the library; nothing is cached.
class UserLibraryProvider : public AssetProvider
{
public:
    UserLibraryProvider(ProviderContext *context, UserLibrary *library, QString collection);

    ProviderInfo info() const override;
    RequestHandle search(const SearchRequest &request, SearchCallback callback) override;
    RequestHandle download(const AssetItem &item, DownloadSpecCallback callback) override;

    static AssetItem toItem(const UserLibrary::Collection &collection, const UserLibrary::Entry &entry);

private:
    QPointer<UserLibrary> m_library;
    QString m_collection;
};

} // namespace dafin::assets
