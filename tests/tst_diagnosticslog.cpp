#include "app/platform/DiagnosticsLog.h"
#include "core/base/ZipArchive.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

// The session log and the diagnostics export (src/app/platform/DiagnosticsLog.*).
class DiagnosticsLogTest : public QObject
{
    Q_OBJECT

private:
    static QByteArray readAll(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    static void touch(const QString &path, const QByteArray &content = "x")
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(content);
    }

private slots:
    void cleanup() { dafin::log::stop(); }

    void pruneKeepsTheNewestSessions()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        for (int day = 1; day <= 7; ++day)
            touch(dir.filePath(QStringLiteral("session-2026010%1-000000-000.log").arg(day)));
        touch(dir.filePath(QStringLiteral("notes.txt"))); // not a session log: never touched

        dafin::log::pruneSessions(dir.path(), 5);

        const QStringList left = dafin::log::sessionLogs(dir.path());
        QCOMPARE(left.size(), 5);
        QVERIFY(left.first().endsWith(QStringLiteral("session-20260107-000000-000.log"))); // newest first
        QVERIFY(left.last().endsWith(QStringLiteral("session-20260103-000000-000.log")));
        QVERIFY(QFile::exists(dir.filePath(QStringLiteral("notes.txt"))));
    }

    void startingASessionKeepsFiveInTotal()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        for (int day = 1; day <= 6; ++day)
            touch(dir.filePath(QStringLiteral("session-2026010%1-000000-000.log").arg(day)));

        dafin::log::start(dir.path());
        QVERIFY(dafin::log::isActive());
        QVERIFY(QFile::exists(dafin::log::currentLogPath()));
        QCOMPARE(dafin::log::sessionLogs(dir.path()).size(), dafin::log::kKeepSessions);
        // The oldest went, not the new one.
        QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("session-20260101-000000-000.log"))));
        QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("session-20260102-000000-000.log"))));
    }

    void recordsHeaderWarningsAndCaughtErrors()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        dafin::log::start(dir.path());
        const QString path = dafin::log::currentLogPath();
        QVERIFY(!path.isEmpty());

        qWarning("diagnostics-test-warning-marker");
        dafin::log::logCaught(QStringLiteral("SomeWhere"), QStringLiteral("it broke"));
        dafin::log::writeBlock(QStringLiteral("System report"), QStringLiteral("GPU: Test GPU, driver 1.2.3"));
        dafin::log::stop();
        QVERIFY(!dafin::log::isActive());

        const QString text = QString::fromUtf8(readAll(path));
        QVERIFY(text.contains(QStringLiteral("app version:")));
        QVERIFY(text.contains(QStringLiteral("os:")));
        QVERIFY(text.contains(QStringLiteral("diagnostics-test-warning-marker")));
        QVERIFY(text.contains(QStringLiteral("caught error in SomeWhere: it broke")));
        QVERIFY(text.contains(QStringLiteral("GPU: Test GPU, driver 1.2.3")));
    }

    void loggingWithoutASessionIsHarmless()
    {
        QVERIFY(!dafin::log::isActive());
        dafin::log::write(QStringLiteral("I"), QStringLiteral("nobody is listening"));
        dafin::log::logCaught(QStringLiteral("Nowhere"), QStringLiteral("still fine"));
        dafin::log::writeBlock(QStringLiteral("t"), QStringLiteral("x"));
        QVERIFY(dafin::log::currentLogPath().isEmpty());
    }

    void aLogFolderThatCannotBeMadeDoesNotStartASession()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        touch(dir.filePath(QStringLiteral("blocker"))); // a file where the folder would go
        dafin::log::start(dir.filePath(QStringLiteral("blocker/logs")));
        QVERIFY(!dafin::log::isActive());
    }

    void exportBundlesLogsReportAndVersion()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString logDir = dir.filePath(QStringLiteral("logs"));
        const QString outDir = dir.filePath(QStringLiteral("out"));
        dafin::log::start(logDir);
        dafin::log::write(QStringLiteral("I"), QStringLiteral("export-marker-line"));
        // Six earlier exports: only the newest five survive the new one.
        QVERIFY(QDir().mkpath(outDir));
        for (int day = 1; day <= 6; ++day)
            touch(QDir(outDir).filePath(QStringLiteral("dafin-diagnostics-2026010%1-000000.zip").arg(day)));

        const dafin::log::ExportResult result =
            dafin::log::exportDiagnostics(QStringLiteral("GPU: Test GPU 9000, driver 7.7"), outDir, logDir);
        QVERIFY2(!result.path.isEmpty(), qPrintable(result.error));
        QVERIFY(QFile::exists(result.path));
        QCOMPARE(QDir(outDir).entryList({QStringLiteral("dafin-diagnostics-*.zip")}, QDir::Files).size(), 5);

        QFile zip(result.path);
        QVERIFY(zip.open(QIODevice::ReadOnly));
        QString error;
        const QList<drift::zip::Entry> entries = drift::zip::readEntries(zip, &error);
        QVERIFY2(!entries.isEmpty(), qPrintable(error));

        QHash<QString, QByteArray> files;
        for (const drift::zip::Entry &entry : entries) {
            QByteArray data;
            QVERIFY(drift::zip::extractEntry(zip, entry, data));
            files.insert(entry.path, data);
        }
        QVERIFY(files.contains(QStringLiteral("version.txt")));
        QVERIFY(files.value(QStringLiteral("version.txt")).startsWith("Dafin "));
        QCOMPARE(files.value(QStringLiteral("system-report.txt")), QByteArray("GPU: Test GPU 9000, driver 7.7"));
        QString logEntry;
        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            if (it.key().startsWith(QStringLiteral("logs/session-")))
                logEntry = it.key();
        }
        QVERIFY(!logEntry.isEmpty());
        QVERIFY(files.value(logEntry).contains("export-marker-line"));
    }
};

QTEST_GUILESS_MAIN(DiagnosticsLogTest)
#include "tst_diagnosticslog.moc"
