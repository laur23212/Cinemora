#include "app/brand/RenameMigration.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace dafin {

namespace {

const QString kOldName = QStringLiteral("Cadra");
const QString kDoneKey = QStringLiteral("migration/fromCadra");

// Where a standard location pointed under the old names. QStandardPaths builds these from the
// application and organization names, so ask it with the old names set, then put ours back.
QString oldLocation(QStandardPaths::StandardLocation location)
{
    const QString app = QCoreApplication::applicationName();
    const QString org = QCoreApplication::organizationName();
    QCoreApplication::setApplicationName(kOldName);
    QCoreApplication::setOrganizationName(kOldName);
    const QString path = QStandardPaths::writableLocation(location);
    QCoreApplication::setApplicationName(app);
    QCoreApplication::setOrganizationName(org);
    return path;
}

bool isMissingOrEmpty(const QString &path)
{
    const QDir dir(path);
    return !dir.exists() || dir.isEmpty(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
}

// Moves the old folder into place when the new one is not in use yet. A rename on the same
// drive is instant even for gigabytes of addons; across drives it fails and the folder stays
// where it was (the app then starts with an empty one, as on a fresh install).
void moveFolder(QStandardPaths::StandardLocation location)
{
    const QString from = oldLocation(location);
    const QString to = QStandardPaths::writableLocation(location);
    if (from.isEmpty() || to.isEmpty() || from == to || !QFileInfo(from).isDir() || !isMissingOrEmpty(to))
        return;
    QDir().rmdir(to); // an empty folder something already created
    QDir().mkpath(QFileInfo(to).absolutePath());
    if (QDir().rename(from, to)) {
        // Remove the now-empty "Cadra" organization folder left behind, if nothing else is in it.
        QDir().rmdir(QFileInfo(from).absolutePath());
    }
}

} // namespace

void migrateFromCadra()
{
    QSettings current;
    if (current.value(kDoneKey).toBool())
        return;

    QSettings old(QSettings::UserScope, kOldName, kOldName);
    const QStringList keys = old.allKeys();
    // Only into fresh settings: a Dafin that already ran keeps its own choices.
    if (!keys.isEmpty() && current.allKeys().isEmpty()) {
        for (const QString &key : keys)
            current.setValue(key, old.value(key));
    }

    moveFolder(QStandardPaths::AppDataLocation);
    moveFolder(QStandardPaths::AppLocalDataLocation);
    moveFolder(QStandardPaths::CacheLocation);

    current.setValue(kDoneKey, true);
    current.sync();
}

} // namespace dafin
