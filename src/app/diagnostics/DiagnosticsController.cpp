#include "app/diagnostics/DiagnosticsController.h"

#include "app/diagnostics/AppLog.h"
#include "app/diagnostics/DiagnosticsReport.h"
#include "engine/diagnostics/DebugReport.h"

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>

#ifndef DRIFT_VERSION
#define DRIFT_VERSION "unknown"
#endif

namespace {

QString reportsDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/diagnostics");
}

void openFolderOf(const QString &path)
{
    if (!path.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
}

} // namespace

DiagnosticsController::DiagnosticsController(QObject *parent)
    : QObject(parent)
{
}

QString DiagnosticsController::logPath() const
{
    return dafin::applog::path();
}

QString DiagnosticsController::repo() const
{
    return dafin::diagnostics::defaultRepo();
}

QString DiagnosticsController::logText() const
{
    return dafin::diagnostics::scrubForThisComputer(dafin::applog::readTail(200 * 1024));
}

void DiagnosticsController::copyLog()
{
    if (QGuiApplication::clipboard())
        QGuiApplication::clipboard()->setText(logText());
}

void DiagnosticsController::copyReport()
{
    if (QGuiApplication::clipboard())
        QGuiApplication::clipboard()->setText(m_report);
}

void DiagnosticsController::showLogFolder()
{
    openFolderOf(logPath());
}

void DiagnosticsController::showReportFolder()
{
    openFolderOf(m_reportPath);
}

void DiagnosticsController::sendDiagnostics()
{
    using namespace dafin::diagnostics;

    Inputs in;
    in.version = QStringLiteral(DRIFT_VERSION);
    in.os = QSysInfo::prettyProductName() + QStringLiteral(" (") + QSysInfo::kernelType() + QLatin1Char(' ')
            + QSysInfo::kernelVersion() + QStringLiteral(", ") + QSysInfo::currentCpuArchitecture() + QLatin1Char(')');
    in.systemText = DebugReport::formatPlainText(DebugReport::collect());
    in.errors = dafin::applog::recentErrors(30);
    in.logTail = dafin::applog::readTail(24 * 1024);

    QString home = QDir::homePath();
    QString user = qEnvironmentVariable("USERNAME");
    if (user.isEmpty())
        user = qEnvironmentVariable("USER");
    m_report = buildReport(in, home, user);

    // Always keep a file, so the report survives a closed browser tab.
    QDir().mkpath(reportsDir());
    m_reportPath = reportsDir() + QStringLiteral("/dafin-diagnostics-")
                   + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss")) + QStringLiteral(".txt");
    {
        QFile file(m_reportPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            file.write(m_report.toUtf8());
        else
            m_reportPath.clear();
    }
    // Keep the newest 10 report files.
    const QFileInfoList old = QDir(reportsDir()).entryInfoList({QStringLiteral("dafin-diagnostics-*.txt")},
                                                               QDir::Files, QDir::Name | QDir::Reversed);
    for (int i = 10; i < old.size(); ++i)
        QFile::remove(old[i].absoluteFilePath());
    emit reportChanged();

    const QString title = QStringLiteral("Diagnostics: Dafin ") + in.version;
    const QString body = QStringLiteral("<!-- Please describe what went wrong above this line. -->\n\n```\n")
                         + m_report + QStringLiteral("\n```\n");
    const IssueLink link = makeIssueLink(defaultRepo(), title, body);
    if (link.truncated && QGuiApplication::clipboard())
        QGuiApplication::clipboard()->setText(m_report);

    const bool opened = QDesktopServices::openUrl(link.url);
    emit reportReady(opened, link.truncated, m_reportPath);
}
