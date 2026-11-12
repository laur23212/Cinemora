#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <optional>

#include "core/project/Project.h"

namespace drift {

// Dafin: a frozen, self-contained copy of a timeline taken at one moment, for rendering later.
//
// It holds the whole project document (clips, effects, keyframes, settings, composites) as an
// independent copy, plus free-form render settings, and serializes to one JSON object. Media is
// referenced by path, never copied, so a snapshot of an hour-long project stays small.
//
// Nothing here knows about the encoder: the export queue renders one, and a later feature can
// save one as an editable "Render timeline" workspace (project() is an ordinary Project that
// opens in the editor). Edits to the live project after capture() never reach a snapshot.
struct RenderSnapshot
{
    static constexpr int kFormatVersion = 1;
    static constexpr const char *kFormatTag = "dafin.render-snapshot";

    QString id;                // Unique per capture (a UUID without braces).
    QString name;              // Display name, usually the project's name.
    QDateTime capturedAt;      // UTC.
    QString sourceProjectPath; // The .drift it came from; empty for an unsaved project.
    QString contentHash;       // Project::contentHash() at capture, to tell copies apart.
    Project project;           // Main timeline active; owns all of its containers.
    QVariantMap renderSettings; // The export dialog's settings map, kept as given.
    QString outputPath;        // Where a render of it goes; may be empty.

    // Copies `live` (its main timeline, even while a composite tab is open) so that nothing the
    // live project does afterwards can change the copy, and so it can be read on another thread.
    static RenderSnapshot capture(const Project &live, const QVariantMap &renderSettings = {},
                                  const QString &outputPath = {},
                                  const QString &sourceProjectPath = {});

    // Absolute paths of the media files the snapshot uses, deduplicated, in asset order.
    QStringList mediaPaths() const;
    // The ones that no longer exist on disk.
    QStringList missingMedia() const;

    QJsonObject toJson() const;
    static std::optional<RenderSnapshot> fromJson(const QJsonObject &object, QString *errorOut = nullptr);

    bool writeToFile(const QString &path, QString *errorOut = nullptr) const;
    static std::optional<RenderSnapshot> readFromFile(const QString &path, QString *errorOut = nullptr);
};

} // namespace drift
