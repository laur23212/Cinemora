// Dafin proxy media: which clips count as heavy, the encoder fallback order, the per-project and
// import settings, and the proxy cache's cap and "Clear proxy cache".
#include "engine/encode/proxies/DafinProxyPolicy.h"
#include "engine/encode/proxies/ReverseProxyCache.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

namespace {

MediaInfo videoInfo(int width, int height, const QString &codec, int bitDepth,
                    qint64 durationUs = 10'000'000, bool alpha = false)
{
    MediaInfo info;
    info.ok = true;
    info.durationUs = durationUs;
    StreamInfo stream;
    stream.type = StreamInfo::Type::Video;
    stream.width = width;
    stream.height = height;
    stream.codecName = codec;
    stream.bitDepth = bitDepth;
    stream.hasAlpha = alpha;
    stream.durationUs = durationUs;
    info.streams.append(stream);
    return info;
}

void writeFile(const QString &path, qint64 bytes, const QDateTime &modified)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(int(bytes), 'x'));
    file.close();
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(modified, QFileDevice::FileModificationTime));
}

} // namespace

class ProxyPolicyTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("DafinTest"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_proxypolicy"));
        QSettings().clear();
    }

    void heavyClipsAreRecognised()
    {
        // 1080p 8-bit H.264 at a normal bitrate is light.
        QVERIFY(dafin::proxy::heavyReasons(videoInfo(1920, 1080, "h264", 8), 20'000'000).isEmpty());
        // 4K.
        QCOMPARE(dafin::proxy::heavyReasons(videoInfo(3840, 2160, "h264", 8), 0),
                 QStringList{QStringLiteral("4K")});
        // Portrait 4K counts by its short side too.
        QCOMPARE(dafin::proxy::heavyReasons(videoInfo(2160, 3840, "h264", 8), 0),
                 QStringList{QStringLiteral("4K")});
        // 8-bit HEVC, which Drift alone did not flag.
        QCOMPARE(dafin::proxy::heavyReasons(videoInfo(1920, 1080, "hevc", 8), 0),
                 QStringList{QStringLiteral("HEVC")});
        // 10-bit H.264.
        QCOMPARE(dafin::proxy::heavyReasons(videoInfo(1920, 1080, "h264", 10), 0),
                 QStringList{QStringLiteral("10-bit")});
        // 4K 10-bit HEVC lists all three.
        QCOMPARE(dafin::proxy::heavyReasons(videoInfo(3840, 2160, "hevc", 10), 0).size(), 3);
        // 100 MB over 10 s = 80 Mbit/s.
        const QStringList bitrate = dafin::proxy::heavyReasons(videoInfo(1920, 1080, "h264", 8),
                                                                100'000'000);
        QCOMPARE(bitrate.size(), 1);
        QVERIFY(bitrate.first().contains(QStringLiteral("80")));
        // Transparency never gets a proxy (it would preview opaque).
        QVERIFY(dafin::proxy::heavyReasons(videoInfo(3840, 2160, "prores", 10, 10'000'000, true), 0)
                    .isEmpty());
        // Cover art is not video.
        MediaInfo cover = videoInfo(3840, 2160, "mjpeg", 8);
        cover.streams[0].attachedPicture = true;
        QVERIFY(dafin::proxy::heavyReasons(cover, 0).isEmpty());
    }

    void encodersFallBackToTheCpu()
    {
        using dafin::proxy::encoderCandidates;
        const QString cpu;
        QCOMPARE(encoderCandidates(QStringLiteral("auto"), true),
                 (QStringList{QStringLiteral("h264_nvenc"), QStringLiteral("h264_amf"),
                              QStringLiteral("h264_qsv"), cpu}));
        QCOMPARE(encoderCandidates(QStringLiteral("auto"), false),
                 (QStringList{QStringLiteral("h264_nvenc"), QStringLiteral("h264_qsv"), cpu}));
        QCOMPARE(encoderCandidates(QStringLiteral("amd"), true),
                 (QStringList{QStringLiteral("h264_amf"), cpu}));
        QCOMPARE(encoderCandidates(QStringLiteral("intel"), false),
                 (QStringList{QStringLiteral("h264_qsv"), cpu}));
        QCOMPARE(encoderCandidates(QStringLiteral("cpu"), true), QStringList{cpu});
        // Every list ends with the CPU.
        for (const QString &pref : {QStringLiteral("auto"), QStringLiteral("nvidia"),
                                    QStringLiteral("amd"), QStringLiteral("intel")}) {
            QVERIFY(encoderCandidates(pref, true).last().isEmpty());
            QVERIFY(encoderCandidates(pref, false).last().isEmpty());
        }
        QVERIFY(dafin::proxy::encoderLabel(QStringLiteral("h264_nvenc")).startsWith("NVIDIA"));
        QVERIFY(dafin::proxy::encoderLabel(QString()).contains(QStringLiteral("CPU")));
    }

    void settingsHaveSafeDefaults()
    {
        QSettings().clear();
        QCOMPARE(dafin::proxy::encoderPreference(), QStringLiteral("auto"));
        QCOMPARE(dafin::proxy::importChoice(), QStringLiteral("ask"));
        dafin::proxy::setImportChoice(QStringLiteral("nonsense"));
        QCOMPARE(dafin::proxy::importChoice(), QStringLiteral("ask"));
        dafin::proxy::setImportChoice(QStringLiteral("full"));
        QCOMPARE(dafin::proxy::importChoice(), QStringLiteral("full"));
        QVERIFY(!dafin::proxy::besideSource());
        QVERIFY(dafin::proxy::cacheLimitChoicesGb().contains(dafin::proxy::cacheLimitGb()));
        dafin::proxy::setCacheLimitGb(20);
        QCOMPARE(dafin::proxy::cacheLimitBytes(), 20LL * 1024 * 1024 * 1024);

        // Per project, default on; an empty id (unsaved, no id yet) is always on.
        const QString id = QStringLiteral("0f1e2d3c-project");
        QVERIFY(dafin::proxy::projectEnabled(id));
        dafin::proxy::setProjectEnabled(id, false);
        QVERIFY(!dafin::proxy::projectEnabled(id));
        QVERIFY(dafin::proxy::projectEnabled(QStringLiteral("another-project")));
        dafin::proxy::setProjectEnabled(id, true);
        QVERIFY(dafin::proxy::projectEnabled(id));
        QVERIFY(dafin::proxy::projectEnabled(QString()));
    }

    void cacheIsCappedAndCleared()
    {
        QTemporaryDir media;
        QVERIFY(media.isValid());
        const QString cacheDir = drift::reverseCacheDir();
        QVERIFY(!cacheDir.isEmpty());

        drift::ReverseProxyCache &cache = drift::ReverseProxyCache::instance();
        const QDateTime now = QDateTime::currentDateTime();
        QStringList sources;
        QStringList proxies;
        for (int i = 0; i < 3; ++i) {
            const QString source = media.filePath(QStringLiteral("clip%1.mp4").arg(i));
            writeFile(source, 10, now);
            const QString proxy = QDir(cacheDir).filePath(QStringLiteral("tst-proxy-%1.mp4").arg(i));
            // Oldest first: clip0's proxy is the oldest.
            writeFile(proxy, 1000, now.addSecs(-300 + i * 100));
            cache.insertPreview(source, 720, proxy);
            sources << source;
            proxies << proxy;
        }
        QCOMPARE(cache.lookupPreview(sources[0], 720), proxies[0]);
        QVERIFY(dafin::proxy::cacheFolderBytes() >= 3000);

        // Room for one: the two oldest go, the newest stays.
        cache.trimToSize(1500);
        QVERIFY(!QFile::exists(proxies[0]));
        QVERIFY(!QFile::exists(proxies[1]));
        QVERIFY(QFile::exists(proxies[2]));
        QVERIFY(cache.lookupPreview(sources[0], 720).isEmpty());
        QCOMPARE(cache.lookupPreview(sources[2], 720), proxies[2]);

        // The newest is kept even when it alone is over the cap.
        cache.trimToSize(10);
        QVERIFY(QFile::exists(proxies[2]));

        // The per-project switch hides proxies from preview without deleting them.
        drift::Clip clip;
        clip.type = drift::ClipType::Video;
        clip.path = sources[2];
        QCOMPARE(drift::videoReadPath(clip, true), proxies[2]);
        // Export never asks for proxies.
        QCOMPARE(drift::videoReadPath(clip, false), sources[2]);
        drift::ReverseProxyCache::projectProxiesEnabled = false;
        QCOMPARE(drift::videoReadPath(clip, true), sources[2]);
        drift::ReverseProxyCache::projectProxiesEnabled = true;

        QCOMPARE(cache.clearPreviews(), 1);
        QVERIFY(!QFile::exists(proxies[2]));
        QVERIFY(cache.lookupPreview(sources[2], 720).isEmpty());
    }
};

QTEST_MAIN(ProxyPolicyTest)
#include "tst_proxypolicy.moc"
