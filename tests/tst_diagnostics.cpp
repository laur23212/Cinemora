#include "app/diagnostics/AppLog.h"
#include "app/diagnostics/DiagnosticsReport.h"

#include <QFile>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>

using namespace dafin;
using namespace dafin::diagnostics;

class DiagnosticsTest : public QObject
{
    Q_OBJECT
private slots:
    void scrubHidesWindowsUserFolder();
    void scrubHidesUnixHomeAndUserName();
    void scrubHidesEmail();
    void scrubLeavesOrdinaryTextAlone();
    void shortReportKeepsWholeUrl();
    void longReportIsCutToFitTheUrl();
    void issueLinkHasTitleAndLabel();
    void repoNamesAreChecked();
    void logRotatesAndStaysBounded();
    void logTailKeepsWholeLines();
};

void DiagnosticsTest::scrubHidesWindowsUserFolder()
{
    const QString out = scrub(QStringLiteral("Failed: C:\\Users\\Laur\\Videos\\clip.mp4 and c:/users/Laur/x"), {}, {});
    QVERIFY(!out.contains(QStringLiteral("Laur")));
    QVERIFY(out.contains(QStringLiteral("Videos")));
    QVERIFY(out.contains(QStringLiteral("<user>")));
}

void DiagnosticsTest::scrubHidesUnixHomeAndUserName()
{
    const QString out = scrub(QStringLiteral("open /home/maria/Projects/a.drift on maria-pc by maria"),
                              QStringLiteral("/home/maria"), QStringLiteral("maria"));
    QVERIFY(!out.contains(QStringLiteral("maria")));
}

void DiagnosticsTest::scrubHidesEmail()
{
    QVERIFY(!scrub(QStringLiteral("contact a.b@example.com now"), {}, {}).contains(QStringLiteral("example.com")));
}

void DiagnosticsTest::scrubLeavesOrdinaryTextAlone()
{
    const QString text = QStringLiteral("GPU: NVIDIA GeForce RTX 3060, driver 551.23\nOpenGL 4.6");
    QCOMPARE(scrub(text, QStringLiteral("/home/x"), QStringLiteral("ab")), text);
}

void DiagnosticsTest::shortReportKeepsWholeUrl()
{
    const IssueLink link = makeIssueLink(QStringLiteral("o/r"), QStringLiteral("T"), QStringLiteral("small body"));
    QVERIFY(!link.truncated);
    QCOMPARE(link.shortBody, QStringLiteral("small body"));
}

void DiagnosticsTest::longReportIsCutToFitTheUrl()
{
    QString body;
    for (int i = 0; i < 4000; ++i)
        body += QStringLiteral("line %1 \u00e9\u00e8 with some text\n").arg(i);
    const IssueLink link = makeIssueLink(QStringLiteral("o/r"), QStringLiteral("T"), body, 7000);
    QVERIFY(link.truncated);
    QVERIFY(link.url.toEncoded().size() <= 7000);
    QVERIFY(link.shortBody.startsWith(QStringLiteral("line 0 ")));
    QVERIFY(link.shortBody.contains(QStringLiteral("clipboard")));
}

void DiagnosticsTest::issueLinkHasTitleAndLabel()
{
    const IssueLink link = makeIssueLink(QStringLiteral("laur23212/Dafin"), QStringLiteral("Diagnostics: Dafin 1"),
                                         QStringLiteral("b"));
    QCOMPARE(link.url.host(), QStringLiteral("github.com"));
    QCOMPARE(link.url.path(), QStringLiteral("/laur23212/Dafin/issues/new"));
    const QUrlQuery q(link.url);
    QCOMPARE(q.queryItemValue(QStringLiteral("labels")), QStringLiteral("diagnostics"));
    QCOMPARE(QUrl::fromPercentEncoding(q.queryItemValue(QStringLiteral("title"), QUrl::FullyEncoded).toUtf8()),
             QStringLiteral("Diagnostics: Dafin 1"));
}

void DiagnosticsTest::repoNamesAreChecked()
{
    QVERIFY(isValidRepo(defaultRepo()));
    QVERIFY(isValidRepo(QStringLiteral("laur23212/Dafin-reports")));
    QVERIFY(!isValidRepo(QStringLiteral("no-slash")));
    QVERIFY(!isValidRepo(QStringLiteral("a/b/c")));
    QVERIFY(!isValidRepo(QStringLiteral("a b/c")));
}

void DiagnosticsTest::logRotatesAndStaysBounded()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RotatingLog log(dir.filePath(QStringLiteral("t.log")), 2048, 2);
    for (int i = 0; i < 300; ++i)
        log.append(QStringLiteral("message number %1 with some padding text to take room").arg(i));
    qint64 total = 0;
    for (const QString &name : {QStringLiteral("t.log"), QStringLiteral("t.log.1"), QStringLiteral("t.log.2")})
        total += QFileInfo(dir.filePath(name)).size();
    QVERIFY(total <= 3 * 2048);
    QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("t.log.3"))));
    QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("t.log.1"))));
    QVERIFY(log.readTail(1 << 20).contains(QStringLiteral("message number 299")));
}

void DiagnosticsTest::logTailKeepsWholeLines()
{
    QTemporaryDir dir;
    RotatingLog log(dir.filePath(QStringLiteral("t.log")), 1 << 20, 2);
    for (int i = 0; i < 100; ++i)
        log.append(QStringLiteral("line-%1-end").arg(i));
    const QString tail = log.readTail(200);
    QVERIFY(tail.size() <= 200);
    for (const QString &line : tail.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
        QVERIFY2(line.startsWith(QStringLiteral("line-")) && line.endsWith(QStringLiteral("-end")), qPrintable(line));
}

QTEST_GUILESS_MAIN(DiagnosticsTest)
#include "tst_diagnostics.moc"
