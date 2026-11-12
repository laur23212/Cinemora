#include "app/editor/ProjectSafety.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>

#include <filesystem>
#include <system_error>

namespace dafin::project {
namespace {

std::filesystem::path toPath(const QString &path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(QFile::encodeName(path).toStdString());
#endif
}

bool hardLink(const QString &from, const QString &to)
{
    std::error_code ec;
    std::filesystem::create_hard_link(toPath(from), toPath(to), ec);
    return !ec;
}

// -<yyyyMMdd>-<HHmmss>-<zzz> between the project's name and its extension.
QRegularExpression backupNamePattern(const QFileInfo &project)
{
    QString pattern = QLatin1Char('^') + QRegularExpression::escape(project.completeBaseName())
                      + QStringLiteral("-\\d{8}-\\d{6}-\\d{3}");
    if (!project.suffix().isEmpty())
        pattern += QStringLiteral("\\.") + QRegularExpression::escape(project.suffix());
    pattern += QLatin1Char('$');
    return QRegularExpression(pattern);
}

} // namespace

// ---- Version stamp and migrations -----------------------------------------------------------

int versionOf(const QJsonObject &root)
{
    return qMax(0, root.value(QLatin1String(kProjectVersionKey)).toInt(0));
}

void stampVersion(QJsonObject &root)
{
    root.insert(QLatin1String(kProjectVersionKey), kProjectVersion);
}

const QList<Migration> &migrations()
{
    static const QList<Migration> steps = {
        // 0 -> 1: files from before the version stamp existed need no change; the step marks where
        // the chain starts.
        {0, [](QJsonObject &) {}},
    };
    return steps;
}

void migrate(QJsonObject &root, const QList<Migration> &steps, int target, bool *fromNewer)
{
    if (fromNewer)
        *fromNewer = false;
    int version = versionOf(root);
    if (version > target) {
        if (fromNewer)
            *fromNewer = true;
        return;
    }
    while (version < target) {
        for (const Migration &step : steps) {
            if (step.from == version && step.apply) {
                step.apply(root);
                break;
            }
        }
        ++version;
    }
    root.insert(QLatin1String(kProjectVersionKey), version);
}

void migrate(QJsonObject &root, bool *fromNewer)
{
    migrate(root, migrations(), kProjectVersion, fromNewer);
}

// ---- Atomic write ---------------------------------------------------------------------------

bool atomicWrite(const QString &path, const QByteArray &data, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    if (file.write(data) != data.size() || !file.flush()) {
        if (error)
            *error = file.errorString();
        file.cancelWriting();
        return false;
    }
    // commit() syncs the temporary file to disk and then renames it over `path`.
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

bool atomicReplace(const QString &stagedPath, const QString &destPath)
{
    // Never remove the current file for a staged one that is not there to replace it.
    if (stagedPath.isEmpty() || !QFile::exists(stagedPath))
        return false;
    std::error_code ec;
    std::filesystem::rename(toPath(stagedPath), toPath(destPath), ec);
    if (!ec)
        return true;
    // A filesystem that refuses to rename over an existing file: the old two-step way.
    if (QFile::exists(destPath))
        QFile::remove(destPath);
    return QFile::rename(stagedPath, destPath);
}

// ---- Backups ----------------------------------------------------------------------------------

QString backupFolder(const QString &projectPath)
{
    return QFileInfo(projectPath).absolutePath() + QStringLiteral("/backups");
}

QStringList backupsOf(const QString &projectPath)
{
    const QFileInfo project(projectPath);
    const QRegularExpression pattern = backupNamePattern(project);
    const QString folder = backupFolder(projectPath);
    QStringList names = QDir(folder).entryList(QDir::Files, QDir::Name | QDir::Reversed);
    QStringList paths;
    for (const QString &name : std::as_const(names)) {
        if (pattern.match(name).hasMatch())
            paths.append(folder + QLatin1Char('/') + name);
    }
    return paths;
}

QString backupBeforeOverwrite(const QString &projectPath,
                              const std::function<bool(const QString &)> &isGood, int keep)
{
    try {
        const QFileInfo project(projectPath);
        if (!project.isFile())
            return {};
        if (isGood && !isGood(projectPath))
            return {};
        const QString folder = backupFolder(projectPath);
        if (!QDir().mkpath(folder))
            return {};

        QString name = project.completeBaseName() + QLatin1Char('-')
                       + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
        if (!project.suffix().isEmpty())
            name += QLatin1Char('.') + project.suffix();
        const QString backupPath = folder + QLatin1Char('/') + name;
        if (QFile::exists(backupPath))
            return {}; // saved twice within a millisecond: the first one already holds this version
        if (!hardLink(projectPath, backupPath)) {
            if (!QFile::copy(projectPath, backupPath)) {
                QFile::remove(backupPath);
                return {};
            }
        }

        const QStringList all = backupsOf(projectPath);
        for (int i = qMax(1, keep); i < all.size(); ++i)
            QFile::remove(all.at(i));
        return backupPath;
    } catch (...) {
        return {};
    }
}

QString newestGoodBackup(const QString &projectPath, const std::function<bool(const QString &)> &isGood)
{
    for (const QString &backup : backupsOf(projectPath)) {
        if (!isGood || isGood(backup))
            return backup;
    }
    return {};
}

// ---- Recovery ---------------------------------------------------------------------------------

bool recoveryIsNewer(const QString &recoveryPath, const QString &projectPath)
{
    if (projectPath.isEmpty())
        return true;
    const QFileInfo project(projectPath);
    // A content:// location or a file that is gone: the snapshot is all there is.
    if (!project.isFile())
        return true;
    const QFileInfo recovery(recoveryPath);
    if (!recovery.exists())
        return false;
    return recovery.lastModified() > project.lastModified();
}

} // namespace dafin::project
