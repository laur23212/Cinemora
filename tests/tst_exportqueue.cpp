#include <QtTest>

#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <atomic>

#include "app/editor/export/ExportQueue.h"
#include "core/project/RenderSnapshot.h"

using drift::RenderSnapshot;

namespace {

drift::Project sampleProject()
{
    drift::Project project;
    project.setName(QStringLiteral("Holiday"));
    project.setResolution(1920, 1080);
    project.setFps(30);
    drift::MediaAsset asset;
    asset.path = QStringLiteral("/media/does-not-exist/beach.mp4");
    asset.name = QStringLiteral("beach.mp4");
    asset.kind = drift::MediaKind::Video;
    asset.durationUs = 5'000'000;
    project.addAsset(asset);
    return project;
}

RenderSnapshot snapshotTo(const QString &path, const QVariantMap &settings = {})
{
    QVariantMap s = settings;
    if (!s.contains(QStringLiteral("videoCodecId")))
        s.insert(QStringLiteral("videoCodecId"), QStringLiteral("h264"));
    return RenderSnapshot::capture(sampleProject(), s, path);
}

// Waits until no job is queued or running.
bool waitIdle(ExportQueue &queue, int timeoutMs = 5000)
{
    return QTest::qWaitFor([&] { return !queue.busy() && queue.pendingCount() == 0; }, timeoutMs);
}

QString statusOf(const ExportQueue &queue, int row)
{
    return queue.data(queue.index(row), ExportQueue::StatusRole).toString();
}

} // namespace

class ExportQueueTest : public QObject
{
    Q_OBJECT

private slots:
    void snapshotIsFrozenAgainstLaterEdits();
    void snapshotRoundTripsThroughJson();
    void snapshotRoundTripsThroughFile();
    void snapshotListsMediaWithoutCopyingIt();
    void snapshotRejectsForeignJson();
    void cpuFallbackCodecs();
    void jobsRunInListOrder();
    void reorderChangesWhatRunsNext();
    void hardwareFailureFallsBackToCpu();
    void cpuFailureIsReportedWithoutRetry();
    void cancelQueuedAndRunning();
    void duplicateOutputIsRefused();
    void blockedQueueWaits();
    void notifierGetsTheResult();
};

void ExportQueueTest::snapshotIsFrozenAgainstLaterEdits()
{
    drift::Project live = sampleProject();
    const RenderSnapshot snapshot = RenderSnapshot::capture(live, {{QStringLiteral("crf"), 20}},
                                                            QStringLiteral("/out/a.mp4"));
    const QString hashAtCapture = snapshot.project.contentHash();
    QCOMPARE(snapshot.contentHash, hashAtCapture);

    live.setName(QStringLiteral("Edited"));
    live.setResolution(1080, 1920);
    drift::MediaAsset more;
    more.path = QStringLiteral("/media/other.mp4");
    live.addAsset(more);

    QCOMPARE(snapshot.project.name(), QStringLiteral("Holiday"));
    QCOMPARE(snapshot.project.width(), 1920);
    QCOMPARE(snapshot.project.assets().size(), 1);
    QCOMPARE(snapshot.project.contentHash(), hashAtCapture);
    QVERIFY(!snapshot.id.isEmpty());
    QVERIFY(snapshot.capturedAt.isValid());
}

void ExportQueueTest::snapshotRoundTripsThroughJson()
{
    const RenderSnapshot original = RenderSnapshot::capture(
        sampleProject(), {{QStringLiteral("videoCodecId"), QStringLiteral("h265")}, {QStringLiteral("crf"), 24}},
        QStringLiteral("/out/b.mp4"), QStringLiteral("/projects/holiday.drift"));
    // Through bytes, as a saved snapshot would be.
    const QByteArray bytes = QJsonDocument(original.toJson()).toJson(QJsonDocument::Compact);
    QString error;
    const std::optional<RenderSnapshot> back = RenderSnapshot::fromJson(QJsonDocument::fromJson(bytes).object(), &error);
    QVERIFY2(back.has_value(), qPrintable(error));
    QCOMPARE(back->id, original.id);
    QCOMPARE(back->name, original.name);
    QCOMPARE(back->outputPath, original.outputPath);
    QCOMPARE(back->sourceProjectPath, original.sourceProjectPath);
    QCOMPARE(back->renderSettings.value(QStringLiteral("videoCodecId")).toString(), QStringLiteral("h265"));
    QCOMPARE(back->renderSettings.value(QStringLiteral("crf")).toInt(), 24);
    QCOMPARE(back->capturedAt, original.capturedAt);
    QCOMPARE(back->project.contentHash(), original.project.contentHash());
}

void ExportQueueTest::snapshotRoundTripsThroughFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("render.json"));
    const RenderSnapshot original = snapshotTo(QStringLiteral("/out/c.mp4"));
    QString error;
    QVERIFY2(original.writeToFile(path, &error), qPrintable(error));
    const std::optional<RenderSnapshot> back = RenderSnapshot::readFromFile(path, &error);
    QVERIFY2(back.has_value(), qPrintable(error));
    QCOMPARE(back->project.contentHash(), original.project.contentHash());
}

void ExportQueueTest::snapshotListsMediaWithoutCopyingIt()
{
    const RenderSnapshot snapshot = snapshotTo(QStringLiteral("/out/d.mp4"));
    QCOMPARE(snapshot.mediaPaths(), QStringList{QStringLiteral("/media/does-not-exist/beach.mp4")});
    QCOMPARE(snapshot.missingMedia(), snapshot.mediaPaths());
    // A reference, not the file: the JSON stays small.
    QVERIFY(QJsonDocument(snapshot.toJson()).toJson(QJsonDocument::Compact).size() < 64 * 1024);
}

void ExportQueueTest::snapshotRejectsForeignJson()
{
    QString error;
    QVERIFY(!RenderSnapshot::fromJson(QJsonObject{{QStringLiteral("format"), QStringLiteral("other")}}, &error));
    QVERIFY(!error.isEmpty());
    QJsonObject newer = snapshotTo(QStringLiteral("/out/e.mp4")).toJson();
    newer.insert(QStringLiteral("version"), RenderSnapshot::kFormatVersion + 1);
    QVERIFY(!RenderSnapshot::fromJson(newer, &error));
}

void ExportQueueTest::cpuFallbackCodecs()
{
    QCOMPARE(ExportQueue::cpuFallbackCodec(QStringLiteral("h264_nvenc")), QStringLiteral("h264"));
    QCOMPARE(ExportQueue::cpuFallbackCodec(QStringLiteral("h264_qsv")), QStringLiteral("h264"));
    QCOMPARE(ExportQueue::cpuFallbackCodec(QStringLiteral("h265_amf")), QStringLiteral("h265"));
    QCOMPARE(ExportQueue::cpuFallbackCodec(QStringLiteral("h265_vaapi")), QStringLiteral("h265"));
    QCOMPARE(ExportQueue::cpuFallbackCodec(QStringLiteral("av1_nvenc")), QStringLiteral("av1_svt"));
    QVERIFY(ExportQueue::cpuFallbackCodec(QStringLiteral("h264")).isEmpty());
    QVERIFY(ExportQueue::cpuFallbackCodec(QStringLiteral("av1_svt")).isEmpty());
    QVERIFY(ExportQueue::cpuFallbackCodec(QStringLiteral("prores")).isEmpty());

    const QVariantMap hw{{QStringLiteral("videoCodecId"), QStringLiteral("h264_nvenc")},
                         {QStringLiteral("videoPreset"), QStringLiteral("p4")},
                         {QStringLiteral("crf"), 21}};
    const QVariantMap cpu = ExportQueue::cpuFallbackSettings(hw);
    QCOMPARE(cpu.value(QStringLiteral("videoCodecId")).toString(), QStringLiteral("h264"));
    QVERIFY(!cpu.contains(QStringLiteral("videoPreset")));
    QCOMPARE(cpu.value(QStringLiteral("crf")).toInt(), 21);
}

void ExportQueueTest::jobsRunInListOrder()
{
    ExportQueue queue;
    QStringList ran;
    QMutex mutex;
    queue.setBlocked(true);
    queue.setRunner([&](const RenderSnapshot &, const QVariantMap &, const QString &out, QString *,
                        const ExportQueue::ProgressFn &progress) {
        progress(0.5);
        progress(1.0);
        QMutexLocker lock(&mutex);
        ran << out;
        return true;
    });
    QVERIFY(!queue.enqueue(snapshotTo(QStringLiteral("/out/1.mp4"))).isEmpty());
    QVERIFY(!queue.enqueue(snapshotTo(QStringLiteral("/out/2.mp4"))).isEmpty());
    QCOMPARE(queue.count(), 2);
    QCOMPARE(queue.pendingCount(), 2);
    queue.setBlocked(false);
    QVERIFY(waitIdle(queue));
    QCOMPARE(ran, (QStringList{QStringLiteral("/out/1.mp4"), QStringLiteral("/out/2.mp4")}));
    QCOMPARE(statusOf(queue, 0), QStringLiteral("done"));
    QCOMPARE(queue.data(queue.index(0), ExportQueue::ProgressRole).toDouble(), 1.0);
}

void ExportQueueTest::reorderChangesWhatRunsNext()
{
    ExportQueue queue;
    QStringList ran;
    QMutex mutex;
    queue.setBlocked(true);
    queue.setRunner([&](const RenderSnapshot &, const QVariantMap &, const QString &out, QString *,
                        const ExportQueue::ProgressFn &) {
        QMutexLocker lock(&mutex);
        ran << out;
        return true;
    });
    queue.enqueue(snapshotTo(QStringLiteral("/out/a.mp4")));
    queue.enqueue(snapshotTo(QStringLiteral("/out/b.mp4")));
    queue.enqueue(snapshotTo(QStringLiteral("/out/c.mp4")));
    queue.move(2, 0); // c, a, b
    QCOMPARE(queue.data(queue.index(0), ExportQueue::OutputPathRole).toString(), QStringLiteral("/out/c.mp4"));
    queue.move(1, 2); // c, b, a
    queue.setBlocked(false);
    QVERIFY(waitIdle(queue));
    QCOMPARE(ran, (QStringList{QStringLiteral("/out/c.mp4"), QStringLiteral("/out/b.mp4"),
                               QStringLiteral("/out/a.mp4")}));
    queue.remove(queue.data(queue.index(1), ExportQueue::JobIdRole).toString());
    QCOMPARE(queue.count(), 2);
    queue.clearFinished();
    QCOMPARE(queue.count(), 0);
}

void ExportQueueTest::hardwareFailureFallsBackToCpu()
{
    ExportQueue queue;
    QStringList codecs;
    QMutex mutex;
    queue.setRunner([&](const RenderSnapshot &, const QVariantMap &settings, const QString &, QString *error,
                        const ExportQueue::ProgressFn &progress) {
        const QString codec = settings.value(QStringLiteral("videoCodecId")).toString();
        {
            QMutexLocker lock(&mutex);
            codecs << codec;
        }
        if (codec.endsWith(QLatin1String("_nvenc"))) {
            *error = QStringLiteral("Could not open the NVIDIA encoder");
            return false;
        }
        progress(1.0);
        return true;
    });
    QSignalSpy finished(&queue, &ExportQueue::jobFinished);
    queue.enqueue(snapshotTo(QStringLiteral("/out/hw.mp4"), {{QStringLiteral("videoCodecId"), QStringLiteral("h264_nvenc")},
                                                             {QStringLiteral("videoPreset"), QStringLiteral("p4")}}));
    QVERIFY(waitIdle(queue));
    QCOMPARE(codecs, (QStringList{QStringLiteral("h264_nvenc"), QStringLiteral("h264")}));
    QCOMPARE(statusOf(queue, 0), QStringLiteral("done"));
    QVERIFY(queue.data(queue.index(0), ExportQueue::UsedCpuFallbackRole).toBool());
    QVERIFY(queue.data(queue.index(0), ExportQueue::StatusTextRole).toString().contains(QLatin1String("CPU")));
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(1).toBool(), true);  // ok
    QCOMPARE(finished.at(0).at(3).toBool(), true);  // usedCpuFallback
}

void ExportQueueTest::cpuFailureIsReportedWithoutRetry()
{
    ExportQueue queue;
    std::atomic<int> runs{0};
    queue.setRunner([&](const RenderSnapshot &, const QVariantMap &, const QString &, QString *error,
                        const ExportQueue::ProgressFn &) {
        ++runs;
        *error = QStringLiteral("Disk full");
        return false;
    });
    queue.enqueue(snapshotTo(QStringLiteral("/out/cpu.mp4")));
    QVERIFY(waitIdle(queue));
    QCOMPARE(runs.load(), 1);
    QCOMPARE(statusOf(queue, 0), QStringLiteral("failed"));
    QVERIFY(queue.data(queue.index(0), ExportQueue::StatusTextRole).toString().contains(QLatin1String("Disk full")));
}

void ExportQueueTest::cancelQueuedAndRunning()
{
    ExportQueue queue;
    std::atomic<bool> started{false};
    std::atomic<int> runs{0};
    queue.setRunner([&](const RenderSnapshot &, const QVariantMap &, const QString &, QString *error,
                        const ExportQueue::ProgressFn &progress) {
        ++runs;
        started = true;
        // Runs until told to stop, like a long render.
        for (int i = 0; i < 2000; ++i) {
            if (!progress(i / 2000.0)) {
                *error = QStringLiteral("Export cancelled");
                return false;
            }
            QThread::msleep(2);
        }
        return true;
    });
    const QString first = queue.enqueue(snapshotTo(QStringLiteral("/out/long.mp4"),
                                                   {{QStringLiteral("videoCodecId"), QStringLiteral("h264_nvenc")}}));
    const QString second = queue.enqueue(snapshotTo(QStringLiteral("/out/next.mp4")));
    queue.cancel(second);
    QCOMPARE(statusOf(queue, 1), QStringLiteral("cancelled"));
    QVERIFY(QTest::qWaitFor([&] { return started.load(); }, 3000));
    QVERIFY(QTest::qWaitFor([&] { return statusOf(queue, 0) == QLatin1String("running"); }, 3000));
    queue.cancel(first);
    QVERIFY(waitIdle(queue));
    QCOMPARE(statusOf(queue, 0), QStringLiteral("cancelled"));
    // A cancelled hardware job is not retried on the CPU, and the cancelled queued job never ran.
    QCOMPARE(runs.load(), 1);
}

void ExportQueueTest::duplicateOutputIsRefused()
{
    ExportQueue queue;
    queue.setBlocked(true);
    queue.setRunner([](const RenderSnapshot &, const QVariantMap &, const QString &, QString *,
                       const ExportQueue::ProgressFn &) { return true; });
    QSignalSpy messages(&queue, &ExportQueue::message);
    QVERIFY(!queue.enqueue(snapshotTo(QStringLiteral("/out/same.mp4"))).isEmpty());
    QVERIFY(queue.enqueue(snapshotTo(QStringLiteral("/out/same.mp4"))).isEmpty());
    QCOMPARE(queue.count(), 1);
    QCOMPARE(messages.count(), 1);
    QVERIFY(queue.enqueue(snapshotTo(QString())).isEmpty());
}

void ExportQueueTest::blockedQueueWaits()
{
    ExportQueue queue;
    std::atomic<int> runs{0};
    queue.setRunner([&](const RenderSnapshot &, const QVariantMap &, const QString &, QString *,
                        const ExportQueue::ProgressFn &) {
        ++runs;
        return true;
    });
    queue.setBlocked(true);
    queue.enqueue(snapshotTo(QStringLiteral("/out/wait.mp4")));
    QTest::qWait(100);
    QCOMPARE(runs.load(), 0);
    QCOMPARE(statusOf(queue, 0), QStringLiteral("queued"));
    queue.setBlocked(false);
    QVERIFY(waitIdle(queue));
    QCOMPARE(runs.load(), 1);
}

void ExportQueueTest::notifierGetsTheResult()
{
    ExportQueue queue;
    queue.setRunner([](const RenderSnapshot &, const QVariantMap &, const QString &, QString *error,
                       const ExportQueue::ProgressFn &) {
        *error = QStringLiteral("Boom");
        return false;
    });
    QStringList titles;
    bool hasAction = false;
    queue.setNotifier([&](const QString &title, const QString &, bool ok, std::function<void()> action) {
        titles << title;
        QVERIFY(!ok);
        hasAction = bool(action);
    });
    queue.enqueue(snapshotTo(QStringLiteral("/out/fail.mp4")));
    QVERIFY(waitIdle(queue));
    QCOMPARE(titles.size(), 1);
    QVERIFY(hasAction);
}

QTEST_MAIN(ExportQueueTest)
#include "tst_exportqueue.moc"
