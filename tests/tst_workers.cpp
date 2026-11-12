// Dafin: the worker-process supervisor (heartbeat timeout, crash, cancel, watchdog restart). The
// test binary is its own stand-in worker: started with --fake-worker <mode>, it plays that part.
#include "app/workers/WorkerChannel.h"
#include "app/workers/WorkerSupervisor.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace drift::worker;

namespace {

// abort() on Windows can raise a dialog that would stall the test; ending the process from
// outside the normal path is what matters here.
[[noreturn]] void crashNow()
{
#ifdef Q_OS_WIN
    TerminateProcess(GetCurrentProcess(), 0xC0000005);
#endif
    std::abort();
}

void sleepMs(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

int fakeWorker(const QString &mode, const QString &arg)
{
    WorkerChannel::setLimitsForTesting(4000, 500, 4000, 300);
    WorkerChannel channel;
    if (mode == QLatin1String("ok")) {
        channel.setPhase(WorkerChannel::Phase::Working);
        channel.progress(0.5);
        sleepMs(300);
        channel.progress(1.0);
        channel.done({{QStringLiteral("value"), 42}});
        return 0;
    }
    if (mode == QLatin1String("noisy")) {
        std::fputs("some library chatter\n@@dafin-worker not json\n", stdout);
        std::fflush(stdout);
        channel.done({{QStringLiteral("value"), 7}});
        return 0;
    }
    if (mode == QLatin1String("crash")) {
        channel.setPhase(WorkerChannel::Phase::Working);
        channel.progress(0.2);
        sleepMs(200);
        crashNow();
    }
    if (mode == QLatin1String("exit3")) {
        return 3;
    }
    if (mode == QLatin1String("hang")) {
        channel.setPhase(WorkerChannel::Phase::Working);
        channel.progress(0.1);
        for (;;)
            sleepMs(1000); // stuck: no progress, so the heartbeat stops
    }
    if (mode == QLatin1String("slowstart")) {
        // Longer than the supervisor's timeout, but the Starting phase keeps beating.
        sleepMs(3500);
        channel.done();
        return 0;
    }
    if (mode == QLatin1String("error")) {
        channel.error(QStringLiteral("the file is unreadable"));
        return 1;
    }
    if (mode == QLatin1String("cancel") || mode == QLatin1String("ignorecancel")) {
        channel.setPhase(WorkerChannel::Phase::Working);
        for (;;) {
            channel.progress(0.3);
            if (mode == QLatin1String("cancel") && channel.cancelled())
                return 0;
            sleepMs(100);
        }
    }
    if (mode == QLatin1String("crashonce")) {
        // First run leaves a marker and crashes; the restarted run finds it and succeeds.
        QFile marker(arg);
        if (!marker.exists()) {
            marker.open(QIODevice::WriteOnly);
            marker.close();
            crashNow();
        }
        channel.done({{QStringLiteral("second"), true}});
        return 0;
    }
    return 2;
}

} // namespace

class TestWorkers : public QObject
{
    Q_OBJECT

    WorkerSpec spec(const QString &mode, const QString &arg = {}, int timeoutMs = 1500)
    {
        WorkerSpec s;
        s.program = QCoreApplication::applicationFilePath();
        s.arguments = {QStringLiteral("--fake-worker"), mode, arg};
        s.heartbeatTimeoutMs = timeoutMs;
        s.cancelGraceMs = 1500;
        return s;
    }

private slots:
    void parsesOnlyWorkerLines()
    {
        QJsonObject message;
        QVERIFY(parseWorkerLine("@@dafin-worker {\"t\":\"hb\"}\r\n", &message));
        QCOMPARE(message.value("t").toString(), QStringLiteral("hb"));
        QVERIFY(!parseWorkerLine("{\"t\":\"hb\"}", &message));
        QVERIFY(!parseWorkerLine("@@dafin-worker garbage", &message));
        QVERIFY(!parseWorkerLine("@@dafin-worker {\"no\":1}", &message));
        QVERIFY(!parseWorkerLine("@@dafin-worker", &message));
    }

    void finishedWorkerReportsResultAndProgress()
    {
        double last = -1;
        WorkerCallbacks cb;
        cb.progress = [&](double v) { last = v; };
        const WorkerResult r = run(spec(QStringLiteral("ok")), cb);
        QCOMPARE(int(r.outcome), int(Outcome::Finished));
        QCOMPARE(r.result.value("value").toInt(), 42);
        QCOMPARE(last, 1.0);
    }

    void ignoresOtherOutput()
    {
        const WorkerResult r = run(spec(QStringLiteral("noisy")));
        QCOMPARE(int(r.outcome), int(Outcome::Finished));
        QCOMPARE(r.result.value("value").toInt(), 7);
    }

    void crashIsReported()
    {
        const WorkerResult r = run(spec(QStringLiteral("crash")));
        QCOMPARE(int(r.outcome), int(Outcome::Crashed));
        QVERIFY(!r.message.isEmpty());
        const WorkerResult e = run(spec(QStringLiteral("exit3")));
        QCOMPARE(int(e.outcome), int(Outcome::Crashed));
        QCOMPARE(e.exitCode, 3);
    }

    void hangIsKilledAfterTheTimeout()
    {
        QElapsedTimer timer;
        timer.start();
        const WorkerResult r = run(spec(QStringLiteral("hang")));
        QCOMPARE(int(r.outcome), int(Outcome::Hung));
        // Beats stop 0.5 s after the last progress; the supervisor waits 1.5 s more.
        QVERIFY2(timer.elapsed() < 8000, qPrintable(QString::number(timer.elapsed())));
    }

    void slowStartIsNotAHang()
    {
        const WorkerResult r = run(spec(QStringLiteral("slowstart")));
        QCOMPARE(int(r.outcome), int(Outcome::Finished));
    }

    void workErrorIsNotRestarted()
    {
        int restarts = 0;
        WorkerCallbacks cb;
        cb.restarting = [&](Outcome, int) { ++restarts; };
        const WorkerResult r = runWithWatchdog(spec(QStringLiteral("error")), 2, cb);
        QCOMPARE(int(r.outcome), int(Outcome::Failed));
        QCOMPARE(r.message, QStringLiteral("the file is unreadable"));
        QCOMPARE(restarts, 0);
    }

    void cancelStopsTheWorker()
    {
        QElapsedTimer timer;
        timer.start();
        WorkerCallbacks cb;
        cb.cancelled = [&] { return timer.elapsed() > 300; };
        const WorkerResult r = run(spec(QStringLiteral("cancel")), cb);
        QCOMPARE(int(r.outcome), int(Outcome::Cancelled));
        QCOMPARE(r.exitCode, 0);
    }

    void workerThatIgnoresCancelIsKilled()
    {
        QElapsedTimer timer;
        timer.start();
        WorkerCallbacks cb;
        cb.cancelled = [&] { return timer.elapsed() > 300; };
        const WorkerResult r = run(spec(QStringLiteral("ignorecancel")), cb);
        QCOMPARE(int(r.outcome), int(Outcome::Cancelled));
        QVERIFY(timer.elapsed() < 6000);
    }

    void watchdogRestartsACrashedWorker()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        int restarts = 0;
        Outcome why = Outcome::Finished;
        WorkerCallbacks cb;
        cb.restarting = [&](Outcome o, int) { ++restarts; why = o; };
        const WorkerResult r =
            runWithWatchdog(spec(QStringLiteral("crashonce"), dir.filePath(QStringLiteral("marker"))), 1, cb);
        QCOMPARE(int(r.outcome), int(Outcome::Finished));
        QVERIFY(r.result.value("second").toBool());
        QCOMPARE(restarts, 1);
        QCOMPARE(r.restarts, 1);
        QCOMPARE(int(why), int(Outcome::Crashed));
    }

    void watchdogGivesUpAfterTheLimit()
    {
        const WorkerResult r = runWithWatchdog(spec(QStringLiteral("crash")), 1);
        QCOMPARE(int(r.outcome), int(Outcome::Crashed));
        QCOMPARE(r.restarts, 1);
    }

    void missingProgramFailsToStart()
    {
        WorkerSpec s;
        s.program = QStringLiteral("/nonexistent/dafin-worker-binary");
        const WorkerResult r = runWithWatchdog(s, 1);
        QCOMPARE(int(r.outcome), int(Outcome::FailedToStart));
        QCOMPARE(r.restarts, 0);
    }
};

int main(int argc, char *argv[])
{
    if (argc >= 3 && qstrcmp(argv[1], "--fake-worker") == 0)
        return fakeWorker(QString::fromLocal8Bit(argv[2]), argc >= 4 ? QString::fromLocal8Bit(argv[3]) : QString());
    QCoreApplication app(argc, argv);
    TestWorkers test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_workers.moc"
