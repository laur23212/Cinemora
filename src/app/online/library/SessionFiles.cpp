#include "app/online/library/SessionFiles.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QStandardPaths>
#include <QUuid>

#include <memory>

namespace dafin::assets::session {

namespace {
constexpr qint64 kStaleSecs = 24 * 3600;
const char kPrefix[] = "session-";

QString baseDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/dafin-assets");
}

struct Session
{
    QString dir;
    std::unique_ptr<QLockFile> lock;
};
Session &current()
{
    static Session s;
    return s;
}
} // namespace

QString root()
{
    Session &s = current();
    if (!s.dir.isEmpty())
        return s.dir;
    const QString dir = baseDir() + QLatin1Char('/') + QLatin1String(kPrefix)
                      + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    QDir().mkpath(dir);
    s.lock = std::make_unique<QLockFile>(dir + QStringLiteral("/.lock"));
    s.lock->setStaleLockTime(0); // held all session; only a dead process makes it stale
    s.lock->tryLock(0);
    s.dir = dir;
    return s.dir;
}

bool isSessionFile(const QString &path)
{
    if (path.isEmpty())
        return false;
    const QString base = QDir::cleanPath(baseDir()) + QLatin1Char('/');
    const QString clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
    return clean.startsWith(base, Qt::CaseInsensitive);
#else
    return clean.startsWith(base);
#endif
}

void remove()
{
    Session &s = current();
    if (s.dir.isEmpty())
        return;
    if (s.lock)
        s.lock->unlock();
    s.lock.reset();
    QDir(s.dir).removeRecursively();
    s.dir.clear();
}

void sweepStale()
{
    const QDir base(baseDir());
    if (!base.exists())
        return;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (const QFileInfo &info : base.entryInfoList({QLatin1String(kPrefix) + QLatin1Char('*')},
                                                    QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (info.absoluteFilePath() == current().dir)
            continue;
        QLockFile lock(info.absoluteFilePath() + QStringLiteral("/.lock"));
        lock.setStaleLockTime(0);
        if (!lock.tryLock(0))
            continue; // a running Dafin owns it
        lock.unlock();
        if (info.lastModified().toUTC().secsTo(now) < kStaleSecs)
            continue; // likely a crash: keep it a day for crash recovery
        QDir(info.absoluteFilePath()).removeRecursively();
    }
}

} // namespace dafin::assets::session
