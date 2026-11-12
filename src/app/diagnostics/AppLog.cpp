#include "app/diagnostics/AppLog.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QtGlobal>

#include <deque>

namespace dafin {

RotatingLog::RotatingLog(const QString &path, qint64 maxBytes, int backups)
    : m_path(path)
    , m_maxBytes(qMax<qint64>(maxBytes, 1024))
    , m_backups(qMax(backups, 1))
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
}

void RotatingLog::rotateLocked()
{
    QFile::remove(m_path + QLatin1Char('.') + QString::number(m_backups));
    for (int i = m_backups - 1; i >= 1; --i)
        QFile::rename(m_path + QLatin1Char('.') + QString::number(i),
                      m_path + QLatin1Char('.') + QString::number(i + 1));
    QFile::rename(m_path, m_path + QStringLiteral(".1"));
}

void RotatingLog::append(const QString &line)
{
    const QByteArray bytes = line.toUtf8() + '\n';
    QMutexLocker lock(&m_mutex);
    if (QFileInfo(m_path).size() + bytes.size() > m_maxBytes)
        rotateLocked();
    QFile file(m_path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append))
        file.write(bytes);
}

QString RotatingLog::readTail(qint64 maxBytes) const
{
    QMutexLocker lock(&m_mutex);
    QByteArray collected;
    // Newest file first, prepending older ones until there is enough.
    for (int i = 0; i <= m_backups && collected.size() < maxBytes; ++i) {
        QFile file(i == 0 ? m_path : m_path + QLatin1Char('.') + QString::number(i));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const qint64 want = maxBytes - collected.size();
        if (file.size() > want)
            file.seek(file.size() - want);
        collected.prepend(file.readAll());
    }
    if (collected.size() > maxBytes)
        collected = collected.right(maxBytes);
    QString text = QString::fromUtf8(collected);
    // The cut may land inside a line: start at the next one.
    if (collected.size() >= maxBytes) {
        const qsizetype nl = text.indexOf(QLatin1Char('\n'));
        if (nl >= 0)
            text.remove(0, nl + 1);
    }
    return text;
}

namespace applog {
namespace {

RotatingLog *g_log = nullptr;
QtMessageHandler g_previous = nullptr;
QMutex g_errorMutex;
std::deque<QString> g_errors;

void handler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    static const char *const kinds[] = {"debug", "warning", "critical", "fatal", "info"};
    const QString kind = QLatin1String(kinds[qBound(0, int(type), 4)]);
    const QString line = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"))
                         + QStringLiteral(" [") + kind + QStringLiteral("] ") + message;
    if (g_log)
        g_log->append(line);
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        QMutexLocker lock(&g_errorMutex);
        g_errors.push_back(line);
        while (g_errors.size() > 100)
            g_errors.pop_front();
    }
    if (g_previous)
        g_previous(type, context, message);
}

} // namespace

void install(const QString &dir)
{
    if (g_log)
        return;
    const QString folder = dir.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/logs")
        : dir;
    g_log = new RotatingLog(folder + QStringLiteral("/dafin.log"), 2 * 1024 * 1024, 3);
    g_log->append(QStringLiteral("---- Dafin log started ") + QDateTime::currentDateTime().toString(Qt::ISODate)
                  + QStringLiteral(" ----"));
    g_previous = qInstallMessageHandler(handler);
}

QString path()
{
    return g_log ? g_log->path() : QString();
}

QString readTail(qint64 maxBytes)
{
    return g_log ? g_log->readTail(maxBytes) : QString();
}

QStringList recentErrors(int limit)
{
    QMutexLocker lock(&g_errorMutex);
    QStringList out;
    const int skip = qMax(0, int(g_errors.size()) - limit);
    for (size_t i = size_t(skip); i < g_errors.size(); ++i)
        out << g_errors[i];
    return out;
}

} // namespace applog
} // namespace dafin
