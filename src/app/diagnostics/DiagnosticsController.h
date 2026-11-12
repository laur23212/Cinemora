#pragma once

#include <QObject>
#include <QString>

// Dafin: QML-facing side of the log viewer and "Send diagnostics" (registered as `Diagnostics`).
// The text work is in DiagnosticsReport.*, the log file in AppLog.*.
class DiagnosticsController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString logPath READ logPath CONSTANT)
    // The last report built by sendDiagnostics(), shown in the fallback window.
    Q_PROPERTY(QString reportText READ reportText NOTIFY reportChanged)
    Q_PROPERTY(QString reportPath READ reportPath NOTIFY reportChanged)
    Q_PROPERTY(QString repo READ repo CONSTANT)

public:
    explicit DiagnosticsController(QObject *parent = nullptr);

    QString logPath() const;
    QString reportText() const { return m_report; }
    QString reportPath() const { return m_reportPath; }
    QString repo() const;

    // The end of the log file (about 200 KB), for the viewer.
    Q_INVOKABLE QString logText() const;
    Q_INVOKABLE void copyLog();
    Q_INVOKABLE void copyReport();
    Q_INVOKABLE void showLogFolder();
    Q_INVOKABLE void showReportFolder();
    // Builds the report, saves it to a file, and opens the browser on a prefilled GitHub issue.
    // Emits reportReady() either way; browserOpened is false when the browser could not start.
    Q_INVOKABLE void sendDiagnostics();

signals:
    void reportChanged();
    // truncated: the link holds a shortened report and the full one is on the clipboard.
    void reportReady(bool browserOpened, bool truncated, const QString &path);

private:
    QString m_report;
    QString m_reportPath;
};
