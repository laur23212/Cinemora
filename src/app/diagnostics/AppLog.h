#pragma once

#include <QByteArray>
#include <QMutex>
#include <QString>
#include <QStringList>

// Dafin: the app's own log file. Every Qt message (qDebug/qInfo/qWarning/...) is also written to
// <AppLocalData>/logs/dafin.log. The file rotates (dafin.log -> dafin.log.1 -> .2 -> .3) when it
// reaches maxBytes, so it can never grow forever. The old message handler (the console, and on
// Windows the crash-report log) keeps getting every message too.

namespace dafin {

// A size-capped log file with numbered backups. Thread-safe. Kept apart from the Qt message
// handler so it can be tested on its own.
class RotatingLog
{
public:
    RotatingLog(const QString &path, qint64 maxBytes, int backups);

    // Appends one line (a newline is added). Rotates first if the line would pass maxBytes.
    void append(const QString &line);
    // The last `maxBytes` of the current file, then of the backups if that is not enough,
    // oldest first. Cuts at a line start.
    QString readTail(qint64 maxBytes) const;
    QString path() const { return m_path; }

private:
    void rotateLocked();

    QString m_path;
    qint64 m_maxBytes;
    int m_backups;
    mutable QMutex m_mutex;
};

namespace applog {

// Installs the Qt message handler. Call once, after the application names are set (they decide
// the folder). Safe to call twice. `dir` overrides the folder (tests).
void install(const QString &dir = QString());
// Full path of dafin.log, or empty before install().
QString path();
// The end of the log as text, at most maxBytes.
QString readTail(qint64 maxBytes);
// The last warnings/errors of this run (oldest first), at most `limit`.
QStringList recentErrors(int limit = 30);

} // namespace applog
} // namespace dafin
