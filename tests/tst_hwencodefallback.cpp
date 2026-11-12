// Dafin: GPU encoder -> CPU fallback decisions. No GPU needed.
#include "engine/encode/export/HwEncodeFallback.h"

#include <QtTest>

using namespace drift::hwencode;

class TestHwEncodeFallback : public QObject
{
    Q_OBJECT

private slots:
    void softwareCodecForEachGpuEncoder_data()
    {
        QTest::addColumn<QString>("gpu");
        QTest::addColumn<QString>("cpu");
        for (const char *vendor : {"nvenc", "qsv", "amf", "vaapi", "videotoolbox", "mediacodec"}) {
            QTest::newRow(qPrintable(QStringLiteral("h264_%1").arg(QLatin1String(vendor))))
                << QStringLiteral("h264_%1").arg(QLatin1String(vendor)) << QStringLiteral("h264");
            QTest::newRow(qPrintable(QStringLiteral("h265_%1").arg(QLatin1String(vendor))))
                << QStringLiteral("h265_%1").arg(QLatin1String(vendor)) << QStringLiteral("h265");
        }
        for (const char *vendor : {"nvenc", "qsv", "amf", "vaapi"}) {
            QTest::newRow(qPrintable(QStringLiteral("av1_%1").arg(QLatin1String(vendor))))
                << QStringLiteral("av1_%1").arg(QLatin1String(vendor)) << QStringLiteral("av1_svt");
        }
    }
    void softwareCodecForEachGpuEncoder()
    {
        QFETCH(QString, gpu);
        QFETCH(QString, cpu);
        QVERIFY(isGpuEncoderId(gpu));
        QCOMPARE(softwareFallbackCodec(gpu), cpu);
    }

    void softwareEncodersHaveNoFallback()
    {
        for (const char *id : {"h264", "h264_10", "h265", "h265_10", "h265_12", "av1_svt", "av1_svt_10",
                               "vp9", "prores", "dnxhr", "ffv1", "", "_nvenc", "nvenc"}) {
            QVERIFY2(!isGpuEncoderId(QLatin1String(id)), id);
            QVERIFY2(softwareFallbackCodec(QLatin1String(id)).isEmpty(), id);
        }
    }

    void retryOnlyWhenItCanHelp()
    {
        const QString nv = QStringLiteral("h264_nvenc");
        const QString driverError = QStringLiteral("Could not open the video encoder: Generic error in an external library");
        QVERIFY(shouldRetryOnCpu(true, nv, true, driverError));
        QVERIFY(shouldRetryOnCpu(true, nv, true, QString()));
        // Switch off: the old behaviour, the export just fails.
        QVERIFY(!shouldRetryOnCpu(false, nv, true, driverError));
        // Already on the CPU: nothing to fall back to.
        QVERIFY(!shouldRetryOnCpu(true, QStringLiteral("h264"), true, driverError));
        // The target was rejected before encoding: the CPU would hit it too.
        QVERIFY(!shouldRetryOnCpu(true, nv, false, driverError));
        QVERIFY(!shouldRetryOnCpu(true, nv, true, QStringLiteral("Export cancelled")));
        QVERIFY(!shouldRetryOnCpu(true, nv, true, QStringLiteral("Cancelled")));
        QVERIFY(!shouldRetryOnCpu(true, nv, true, QStringLiteral("Could not write: No space left on device")));
        QVERIFY(!shouldRetryOnCpu(true, nv, true, QStringLiteral("Could not open output: permission denied")));
    }
};

QTEST_GUILESS_MAIN(TestHwEncodeFallback)
#include "tst_hwencodefallback.moc"
