#include "core/project/sources/MogrtSource.h"

namespace drift {

QJsonObject MogrtSource::toJson() const
{
    QJsonObject o{
        {QStringLiteral("templateId"), templateId},
        {QStringLiteral("templatePath"), templatePath},
        {QStringLiteral("name"), name},
        {QStringLiteral("controls"), controls},
        {QStringLiteral("values"), values},
        {QStringLiteral("width"), width},
        {QStringLiteral("height"), height},
        {QStringLiteral("fps"), fps},
        {QStringLiteral("templateDurationUs"), static_cast<double>(templateDurationUs)},
    };
    if (!authorApp.isEmpty())
        o.insert(QStringLiteral("authorApp"), authorApp);
    if (!renderKey.isEmpty()) {
        o.insert(QStringLiteral("renderKey"), renderKey);
        o.insert(QStringLiteral("renderQuality"), renderQuality);
    }
    if (!status.isEmpty())
        o.insert(QStringLiteral("status"), status);
    if (!warnings.isEmpty())
        o.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(warnings));
    if (!lastError.isEmpty())
        o.insert(QStringLiteral("lastError"), lastError);
    return o;
}

MogrtSource MogrtSource::fromJson(const QJsonObject &o)
{
    MogrtSource s;
    if (o.isEmpty())
        return s;
    s.templateId = o.value(QStringLiteral("templateId")).toString();
    s.templatePath = o.value(QStringLiteral("templatePath")).toString();
    s.name = o.value(QStringLiteral("name")).toString();
    s.authorApp = o.value(QStringLiteral("authorApp")).toString();
    s.controls = o.value(QStringLiteral("controls")).toArray();
    s.values = o.value(QStringLiteral("values")).toObject();
    s.width = o.value(QStringLiteral("width")).toInt();
    s.height = o.value(QStringLiteral("height")).toInt();
    s.fps = o.value(QStringLiteral("fps")).toDouble();
    s.templateDurationUs = static_cast<int64_t>(o.value(QStringLiteral("templateDurationUs")).toDouble());
    s.renderKey = o.value(QStringLiteral("renderKey")).toString();
    s.renderQuality = o.value(QStringLiteral("renderQuality")).toString();
    s.status = o.value(QStringLiteral("status")).toString();
    for (const QJsonValue &w : o.value(QStringLiteral("warnings")).toArray())
        s.warnings.append(w.toString());
    s.lastError = o.value(QStringLiteral("lastError")).toString();
    return s;
}

} // namespace drift
