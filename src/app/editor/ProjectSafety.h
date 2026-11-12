#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

// Dafin: the pieces that keep a project (and the autosave beside it) from being lost to a crash, a
// full disk or a bad file: an atomic write, rotating backups beside the project, a version number
// in the project document with a migration hook, and the rule for when a recovery snapshot is
// worth offering. The project bundle itself is already written through QSaveFile
// (ProjectBundle.cpp); this is what surrounds that write.

namespace dafin::project {

// ---- Version stamp and migrations -----------------------------------------------------------
// "projectVersion" in the project document. Files written before it existed count as version 0.
inline constexpr int kProjectVersion = 1;
inline constexpr char kProjectVersionKey[] = "projectVersion";

int versionOf(const QJsonObject &root);
void stampVersion(QJsonObject &root);

// One step upgrades a document from `from` to `from + 1`, in place.
struct Migration
{
    int from = 0;
    std::function<void(QJsonObject &)> apply;
};

// The steps this build knows. When the file format changes in a way old files need fixing for, bump
// kProjectVersion and add the step here; a version with no step is simply restamped.
const QList<Migration> &migrations();

// Brings `root` up to `target` by running the steps in order and restamping it. A document from a
// newer version than `target` is left alone (`*fromNewer` set): opening it is still tried, since a
// newer build mostly only adds fields.
void migrate(QJsonObject &root, const QList<Migration> &steps, int target, bool *fromNewer = nullptr);
void migrate(QJsonObject &root, bool *fromNewer = nullptr);

// ---- Atomic write ---------------------------------------------------------------------------
// Writes `data` to a temporary file in the same folder, flushes it to disk and renames it over
// `path`, so `path` is always either the old file or the new one, never half of one (QSaveFile).
bool atomicWrite(const QString &path, const QByteArray &data, QString *error = nullptr);
// Renames `stagedPath` over `destPath` in one step (no moment without a file at `destPath`).
bool atomicReplace(const QString &stagedPath, const QString &destPath);

// ---- Backups ----------------------------------------------------------------------------------
inline constexpr int kKeepBackups = 5;

// <project folder>/backups
QString backupFolder(const QString &projectPath);
// This project's backups, newest first (full paths).
QStringList backupsOf(const QString &projectPath);
// Keeps the file about to be overwritten as <name>-<time>.<ext> in the backups folder and trims
// the folder to the newest `keep`. A hard link where the filesystem allows one (no copy of a large
// bundle; the save replaces the file rather than rewriting it, so the link keeps the old bytes),
// else a copy. `isGood`, when given, vets the existing file first: a damaged file never pushes a
// good backup out. Returns the backup's path, or an empty string when none was made.
QString backupBeforeOverwrite(const QString &projectPath,
                              const std::function<bool(const QString &)> &isGood = {},
                              int keep = kKeepBackups);
// The newest backup that `isGood` accepts, or an empty string.
QString newestGoodBackup(const QString &projectPath,
                         const std::function<bool(const QString &)> &isGood);

// ---- Recovery ---------------------------------------------------------------------------------
// Whether a recovery snapshot should be offered: when the project it was taken of has no file
// (never saved, moved, deleted) or has not been saved since the snapshot. A snapshot older than
// the saved file describes work that file already contains.
bool recoveryIsNewer(const QString &recoveryPath, const QString &projectPath);

} // namespace dafin::project
