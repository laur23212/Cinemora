#include "core/project/RenderSnapshot.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

namespace drift {

RenderSnapshot RenderSnapshot::capture(const Project &live, const QVariantMap &renderSettings,
                                       const QString &outputPath, const QString &sourceProjectPath)
{
    RenderSnapshot snapshot;
    snapshot.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    snapshot.name = live.name();
    snapshot.capturedAt = QDateTime::currentDateTimeUtc();
    snapshot.sourceProjectPath = sourceProjectPath;
    // sequenceView({}) is the main timeline whatever tab is open; detachedCopy() breaks Qt's
    // implicit sharing so a later edit (or a read on the render thread) can't touch the other.
    snapshot.project = live.sequenceView({}).detachedCopy();
    snapshot.contentHash = snapshot.project.contentHash();
    snapshot.renderSettings = renderSettings;
    snapshot.outputPath = outputPath;
    return snapshot;
}

QStringList RenderSnapshot::mediaPaths() const
{
    QStringList out;
    QSet<QString> seen;
    const auto add = [&](const QString &path) {
        if (path.isEmpty() || seen.contains(path))
            return;
        seen.insert(path);
        out.append(path);
    };
    for (const QString &assetId : project.assetOrder()) {
        if (const MediaAsset *asset = project.asset(assetId))
            add(asset->path);
    }
    // Assets outside the bin order (rare, but the hash allows it) still count.
    for (auto it = project.assets().cbegin(); it != project.assets().cend(); ++it)
        add(it->path);
    return out;
}

QStringList RenderSnapshot::missingMedia() const
{
    QStringList out;
    for (const QString &path : mediaPaths()) {
        if (!QFileInfo::exists(path))
            out.append(path);
    }
    return out;
}

QJsonObject RenderSnapshot::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("format"), QString::fromLatin1(kFormatTag));
    object.insert(QStringLiteral("version"), kFormatVersion);
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("capturedAt"), capturedAt.toString(Qt::ISODateWithMs));
    object.insert(QStringLiteral("sourceProjectPath"), sourceProjectPath);
    object.insert(QStringLiteral("contentHash"), contentHash);
    object.insert(QStringLiteral("outputPath"), outputPath);
    object.insert(QStringLiteral("renderSettings"), QJsonObject::fromVariantMap(renderSettings));
    object.insert(QStringLiteral("project"), project.toJson(/*includeTranscripts=*/true));
    return object;
}

std::optional<RenderSnapshot> RenderSnapshot::fromJson(const QJsonObject &object, QString *errorOut)
{
    const auto fail = [errorOut](const QString &message) -> std::optional<RenderSnapshot> {
        if (errorOut)
            *errorOut = message;
        return std::nullopt;
    };
    if (object.value(QStringLiteral("format")).toString() != QLatin1String(kFormatTag))
        return fail(QStringLiteral("Not a render snapshot"));
    const int version = object.value(QStringLiteral("version")).toInt();
    if (version < 1 || version > kFormatVersion)
        return fail(QStringLiteral("Render snapshot version %1 is not supported").arg(version));
    const QJsonValue projectValue = object.value(QStringLiteral("project"));
    if (!projectValue.isObject())
        return fail(QStringLiteral("The render snapshot has no timeline"));

    QString projectError;
    RenderSnapshot snapshot;
    snapshot.project = Project::fromJson(projectValue.toObject(), &projectError);
    if (!projectError.isEmpty())
        return fail(projectError);
    snapshot.id = object.value(QStringLiteral("id")).toString();
    snapshot.name = object.value(QStringLiteral("name")).toString();
    snapshot.capturedAt =
        QDateTime::fromString(object.value(QStringLiteral("capturedAt")).toString(), Qt::ISODateWithMs);
    snapshot.sourceProjectPath = object.value(QStringLiteral("sourceProjectPath")).toString();
    snapshot.contentHash = object.value(QStringLiteral("contentHash")).toString();
    snapshot.outputPath = object.value(QStringLiteral("outputPath")).toString();
    snapshot.renderSettings = object.value(QStringLiteral("renderSettings")).toObject().toVariantMap();
    return snapshot;
}

bool RenderSnapshot::writeToFile(const QString &path, QString *errorOut) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorOut)
            *errorOut = file.errorString();
        return false;
    }
    file.write(QJsonDocument(toJson()).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        if (errorOut)
            *errorOut = file.errorString();
        return false;
    }
    return true;
}

std::optional<RenderSnapshot> RenderSnapshot::readFromFile(const QString &path, QString *errorOut)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorOut)
            *errorOut = file.errorString();
        return std::nullopt;
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorOut)
            *errorOut = parseError.errorString();
        return std::nullopt;
    }
    return fromJson(doc.object(), errorOut);
}

} // namespace drift
