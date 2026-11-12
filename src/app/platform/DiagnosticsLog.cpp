#include "app/platform/DiagnosticsLog.h"

#include "core/base/ZipArchive.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>

#include <cstdio>
#include <cstdlib>
#include <exception>

#ifndef DRIFT_VERSION
#define DRIFT_VERSION "unknown"
#endif

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(Q_OS_UNIX) && !defined(Q_OS_ANDROID) && !defined(Q_OS_WASM) && __has_include(<execinfo.h>)
#include <execinfo.h>
#define DAFIN_HAVE_BACKTRACE 1
#endif

namespace dafin::log {
namespace {

// A session that logs this much (a verbose run) stops recording instead of filling the disk.
constexpr qint64 kMaxSessionBytes = 20 * 1024 * 1024;
constexpr int kKeepExports = 5;

// Leaked on purpose: a message can arrive while statics are being destroyed at exit.
QMutex &mutex()
{
    static QMutex *m = new QMutex;
    return *m;
}

QString &currentPathRef()
{
    static QString *path = new QString;
    return *path;
}

QFile *g_file = nullptr;
qint64 g_written = 0;
bool g_capped = false;
QtMessageHandler g_previousHandler = nullptr;
std::terminate_handler g_previousTerminate = nullptr;
thread_local bool t_inHandler = false;

const char *levelTag(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return "D";
    case QtInfoMsg: return "I";
    case QtWarningMsg: return "W";
    case QtCriticalMsg: return "E";
    case QtFatalMsg: return "F";
    }
    return "?";
}

bool isProblem(QtMsgType type)
{
    return type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg;
}

// Caller holds the mutex.
void appendLocked(const QByteArray &line, bool flush)
{
    if (!g_file || g_capped)
        return;
    if (g_written + line.size() > kMaxSessionBytes) {
        g_capped = true;
        g_file->write("... log size limit reached, later messages are not recorded ...\n");
        g_file->flush();
        return;
    }
    g_file->write(line);
    g_written += line.size();
    if (flush)
        g_file->flush();
}

void writeLine(const QString &level, const QString &message, bool flush)
{
    try {
        const QString stamp =
            QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
        QByteArray line = (stamp + QLatin1Char(' ') + level + QLatin1Char(' ') + message).toUtf8();
        line.append('\n');
        QMutexLocker lock(&mutex());
        appendLocked(line, flush);
    } catch (...) {
        // Logging must never be the thing that fails.
    }
}

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    // A message produced while writing one (an allocation warning, say) is passed on, not logged.
    if (!t_inHandler) {
        t_inHandler = true;
        try {
            QString text = message;
            if (context.category && qstrcmp(context.category, "default") != 0)
                text = QStringLiteral("[%1] %2").arg(QLatin1String(context.category), message);
            if (type == QtCriticalMsg || type == QtFatalMsg) {
                const QString stack = stackTrace(2);
                if (!stack.isEmpty())
                    text += QLatin1Char('\n') + stack;
            }
            writeLine(QLatin1String(levelTag(type)), text, isProblem(type));
        } catch (...) {
        }
        t_inHandler = false;
    }

    if (g_previousHandler) {
        g_previousHandler(type, context, message);
        return;
    }
    // Qt's own handler is not reachable once replaced; this is what it prints.
    const QByteArray out = qFormatLogMessage(type, context, message).toLocal8Bit();
    std::fprintf(stderr, "%s\n", out.constData());
    std::fflush(stderr);
    if (type == QtFatalMsg)
        std::abort();
}

[[noreturn]] void onTerminate()
{
    try {
        QString what = QStringLiteral("terminate called");
        if (std::current_exception()) {
            try {
                throw;
            } catch (const std::exception &e) {
                what = QStringLiteral("uncaught exception: %1").arg(QString::fromUtf8(e.what()));
            } catch (...) {
                what = QStringLiteral("uncaught exception of unknown type");
            }
        }
        logCaught(QStringLiteral("std::terminate"), what);
    } catch (...) {
    }
    if (g_previousTerminate)
        g_previousTerminate();
    std::abort();
}

QString exportsDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
           + QStringLiteral("/diagnostics");
}

QString versionText()
{
    QStringList lines;
    lines << QStringLiteral("Dafin %1").arg(QStringLiteral(DRIFT_VERSION));
    lines << QStringLiteral("OS: %1 (kernel %2 %3, %4)")
                 .arg(QSysInfo::prettyProductName(), QSysInfo::kernelType(),
                      QSysInfo::kernelVersion(), QSysInfo::currentCpuArchitecture());
    lines << QStringLiteral("Qt: %1 (built with %2)")
                 .arg(QString::fromLatin1(qVersion()), QStringLiteral(QT_VERSION_STR));
    lines << QStringLiteral("Locale: %1").arg(QLocale::system().name());
    lines << QStringLiteral("Exported: %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate));
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

} // namespace

QString defaultLogDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return base.isEmpty() ? QString() : base + QStringLiteral("/logs");
}

QStringList sessionLogs(const QString &dir)
{
    QStringList paths;
    const QStringList names = QDir(dir).entryList({QStringLiteral("session-*.log")}, QDir::Files,
                                                  QDir::Name | QDir::Reversed);
    for (const QString &name : names)
        paths.append(dir + QLatin1Char('/') + name);
    return paths;
}

void pruneSessions(const QString &dir, int keep)
{
    try {
        const QStringList logs = sessionLogs(dir);
        for (int i = qMax(0, keep); i < logs.size(); ++i)
            QFile::remove(logs.at(i));
    } catch (...) {
    }
}

void start(const QString &dirOverride)
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_WASM)
    // Android has logcat, which Qt's default handler feeds; replacing it would silence that.
    Q_UNUSED(dirOverride);
#else
    try {
        {
            QMutexLocker lock(&mutex());
            if (g_file)
                return;
        }
        const QString dir = dirOverride.isEmpty() ? defaultLogDir() : dirOverride;
        if (dir.isEmpty() || !QDir().mkpath(dir))
            return;
        // Room for the file about to be made.
        pruneSessions(dir, kKeepSessions - 1);

        const QString path = dir + QStringLiteral("/session-")
                             + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"))
                             + QStringLiteral(".log");
        auto *file = new QFile(path);
        if (!file->open(QIODevice::WriteOnly | QIODevice::Append)) {
            delete file;
            return;
        }
        {
            QMutexLocker lock(&mutex());
            if (g_file) {
                delete file;
                return;
            }
            g_file = file;
            g_written = 0;
            g_capped = false;
            currentPathRef() = path;
        }

        write(QStringLiteral("I"), QStringLiteral("Dafin session log, started %1")
                                       .arg(QDateTime::currentDateTime().toString(Qt::ISODate)));
        write(QStringLiteral("I"), QStringLiteral("app version: %1").arg(QStringLiteral(DRIFT_VERSION)));
        write(QStringLiteral("I"),
              QStringLiteral("os: %1, kernel %2 %3, cpu %4")
                  .arg(QSysInfo::prettyProductName(), QSysInfo::kernelType(), QSysInfo::kernelVersion(),
                       QSysInfo::currentCpuArchitecture()));
        write(QStringLiteral("I"), QStringLiteral("qt: %1 (built with %2)")
                                       .arg(QString::fromLatin1(qVersion()), QStringLiteral(QT_VERSION_STR)));

        g_previousHandler = qInstallMessageHandler(messageHandler);
        g_previousTerminate = std::set_terminate(onTerminate);
    } catch (...) {
    }
#endif
}

void stop()
{
    try {
        // Put the handlers back first so nothing new reaches the file being closed.
        if (g_file) {
            qInstallMessageHandler(g_previousHandler);
            std::set_terminate(g_previousTerminate);
            g_previousHandler = nullptr;
            g_previousTerminate = nullptr;
        }
        QMutexLocker lock(&mutex());
        if (g_file) {
            g_file->flush();
            g_file->close();
            delete g_file;
            g_file = nullptr;
        }
        currentPathRef().clear();
    } catch (...) {
    }
}

bool isActive()
{
    QMutexLocker lock(&mutex());
    return g_file != nullptr;
}

QString currentLogPath()
{
    QMutexLocker lock(&mutex());
    return currentPathRef();
}

void write(const QString &level, const QString &message)
{
    writeLine(level, message, true);
}

void writeBlock(const QString &title, const QString &text)
{
    writeLine(QStringLiteral("I"), QStringLiteral("--- %1 ---\n%2\n--- end ---").arg(title, text), true);
}

void logCaught(const QString &where, const QString &what)
{
    try {
        QString text = QStringLiteral("caught error in %1: %2").arg(where, what);
        const QString stack = stackTrace(1);
        if (!stack.isEmpty())
            text += QLatin1Char('\n') + stack;
        writeLine(QStringLiteral("E"), text, true);
    } catch (...) {
    }
}

QString stackTrace(int skipFrames)
{
    try {
        constexpr int kMaxFrames = 48;
        const int skip = qMax(0, skipFrames) + 1; // this function's own frame
#if defined(Q_OS_WIN)
        void *frames[kMaxFrames];
        const USHORT count = CaptureStackBackTrace(DWORD(skip), kMaxFrames, frames, nullptr);
        QStringList lines;
        for (USHORT i = 0; i < count; ++i) {
            HMODULE module = nullptr;
            wchar_t name[MAX_PATH] = L"?";
            if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                       | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCWSTR>(frames[i]), &module)
                && module) {
                GetModuleFileNameW(module, name, MAX_PATH);
            }
            const qulonglong offset = qulonglong(reinterpret_cast<quintptr>(frames[i]))
                                      - qulonglong(reinterpret_cast<quintptr>(module));
            lines << QStringLiteral("  at %1+0x%2")
                         .arg(QFileInfo(QString::fromWCharArray(name)).fileName())
                         .arg(offset, 0, 16);
        }
        return lines.join(QLatin1Char('\n'));
#elif defined(DAFIN_HAVE_BACKTRACE)
        void *frames[kMaxFrames];
        const int count = backtrace(frames, kMaxFrames);
        char **symbols = backtrace_symbols(frames, count);
        QStringList lines;
        for (int i = skip; i < count; ++i)
            lines << QStringLiteral("  at %1").arg(symbols ? QString::fromLocal8Bit(symbols[i])
                                                           : QStringLiteral("?"));
        std::free(symbols);
        return lines.join(QLatin1Char('\n'));
#else
        Q_UNUSED(skip);
        return {};
#endif
    } catch (...) {
        return {};
    }
}

ExportResult exportDiagnostics(const QString &systemReport, const QString &outDirOverride,
                               const QString &logDirOverride)
{
    ExportResult result;
    try {
        {
            QMutexLocker lock(&mutex());
            if (g_file)
                g_file->flush();
        }
        const QString logDir = logDirOverride.isEmpty() ? defaultLogDir() : logDirOverride;
        const QString outDir = outDirOverride.isEmpty() ? exportsDir() : outDirOverride;
        if (outDir.isEmpty() || !QDir().mkpath(outDir)) {
            result.error = QStringLiteral("Can't create the diagnostics folder");
            return result;
        }
        const QString zipPath = outDir + QStringLiteral("/dafin-diagnostics-")
                                + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))
                                + QStringLiteral(".zip");

        QSaveFile out(zipPath);
        if (!out.open(QIODevice::WriteOnly)) {
            result.error = QStringLiteral("Can't write %1").arg(zipPath);
            return result;
        }
        drift::zip::Writer zip(&out);
        bool ok = zip.addData(QStringLiteral("version.txt"), versionText().toUtf8());
        ok = ok && zip.addData(QStringLiteral("system-report.txt"), systemReport.toUtf8());
        for (const QString &log : sessionLogs(logDir)) {
            if (!ok)
                break;
            ok = zip.addFile(QStringLiteral("logs/") + QFileInfo(log).fileName(), log);
        }
#ifdef Q_OS_WIN
        // The crash reports the Windows handler leaves (CrashReport_win.cpp), newest five.
        const QString crashDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                                 + QStringLiteral("/Dafin/crash");
        const QStringList crashes = QDir(crashDir).entryList({QStringLiteral("crash-*.txt")}, QDir::Files,
                                                              QDir::Name | QDir::Reversed);
        for (int i = 0; ok && i < qMin(5, int(crashes.size())); ++i)
            ok = zip.addFile(QStringLiteral("crash/") + crashes.at(i), crashDir + QLatin1Char('/') + crashes.at(i));
#endif
        ok = ok && zip.finish();
        if (!ok) {
            result.error = zip.errorString().isEmpty() ? QStringLiteral("Can't build the zip") : zip.errorString();
            return result;
        }
        if (!out.commit()) {
            result.error = out.errorString();
            return result;
        }
        result.path = zipPath;

        // Older exports go, so the folder doesn't grow with every bug report.
        const QStringList exports = QDir(outDir).entryList({QStringLiteral("dafin-diagnostics-*.zip")},
                                                            QDir::Files, QDir::Name | QDir::Reversed);
        for (int i = kKeepExports; i < exports.size(); ++i)
            QFile::remove(outDir + QLatin1Char('/') + exports.at(i));
    } catch (...) {
        result.path.clear();
        result.error = QStringLiteral("Unexpected error while exporting diagnostics");
    }
    return result;
}

bool openContainingFolder(const QString &path)
{
    try {
        return QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    } catch (...) {
        return false;
    }
}

} // namespace dafin::log
