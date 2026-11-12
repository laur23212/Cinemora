#pragma once

#include <QString>
#include <QStringList>

// Dafin: a log file that survives the session, on every platform (CrashReport_win.cpp keeps only
// the last run's Qt messages, and only on Windows). One file per session in <AppLocalData>/logs,
// the newest five kept. It records the app version, the OS and — once the system report is known —
// the GPU and driver, every Qt warning/critical message, and the errors reported through
// logCaught() or std::terminate with a stack trace. Nothing here may take the app down: every
// write is wrapped, and a failure to log is silently dropped.

namespace dafin::log {

inline constexpr int kKeepSessions = 5;

// Opens this session's log, drops the oldest ones past kKeepSessions and routes Qt's messages into
// it (the previous handler still gets every message). Call after the application and organisation
// names are set. `dir` overrides the default folder (tests). Harmless to call twice.
void start(const QString &dir = QString());
// Flushes and closes the file; later messages are only passed on.
void stop();
bool isActive();
QString currentLogPath();
QString defaultLogDir();

// One line / one block in the log. No-ops while no log is open.
void write(const QString &level, const QString &message);
void writeBlock(const QString &title, const QString &text);

// Records an error that was caught (or is about to be swallowed) with the stack at this call.
void logCaught(const QString &where, const QString &what);
// The calling thread's stack, one "  at module+0xoffset" / symbol line per frame; empty where the
// platform offers no way to get it.
QString stackTrace(int skipFrames = 0);

// Session logs in `dir`, newest first, and trimming to the newest `keep`.
QStringList sessionLogs(const QString &dir);
void pruneSessions(const QString &dir, int keep);

struct ExportResult
{
    QString path;  // the zip, empty on failure
    QString error; // why, when path is empty
};
// Bundles the session logs, `systemReport` (version, OS, GPU, codecs — DebugReport's text), a
// version file and, on Windows, the crash reports into one zip. `outDir` / `logDir` override the
// defaults (tests). Keeps the newest five exports.
ExportResult exportDiagnostics(const QString &systemReport, const QString &outDir = QString(),
                               const QString &logDir = QString());
// Opens the folder holding `path` in the file manager.
bool openContainingFolder(const QString &path);

} // namespace dafin::log
