#pragma once

#include "app/online/library/AssetProvider.h"

#include <QJsonObject>

namespace cadra::assets {

// One source from the user-editable catalog JSON (the format the Stock Media tab introduced:
// "static" lists of files, or "api" sources described by URL templates and field paths). Each
// catalog source becomes its own provider, id "catalog-<sourceId>", so user-added sources show
// up in the panels and in Settings exactly like the built-in ones.
class CatalogProvider : public AssetProvider
{
public:
    CatalogProvider(ProviderContext *context, QJsonObject source);

    ProviderInfo info() const override;
    RequestHandle search(const SearchRequest &request, SearchCallback callback) override;

    static QJsonValue lookup(const QJsonValue &root, const QString &path);
    static QList<AssetItem> mapResults(const QJsonObject &source, const QString &providerId,
                                       const QJsonDocument &doc);
    static AssetKind kindOf(const QJsonObject &source);

private:
    QString expand(const QString &tmpl, const QString &query, int page) const;

    QJsonObject m_source;
    QString m_id;
};

} // namespace cadra::assets
