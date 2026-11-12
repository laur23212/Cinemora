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

    // Day 6: the full chain.
    void familyChainOrder()
    {
        const QStringList h264 = familyChain(QStringLiteral("h264"));
        QCOMPARE(h264.mid(0, 4), (QStringList{QStringLiteral("h264_nvenc"), QStringLiteral("h264_amf"),
                                              QStringLiteral("h264_qsv"), QStringLiteral("h264_vaapi")}));
        QCOMPARE(h264.last(), QStringLiteral("h264"));
        QCOMPARE(familyChain(QStringLiteral("h265_amf")).last(), QStringLiteral("h265"));
        const QStringList av1 = familyChain(QStringLiteral("av1_qsv"));
        QCOMPARE(av1.last(), QStringLiteral("av1_svt"));
        QVERIFY(!av1.contains(QStringLiteral("av1_videotoolbox")));
        QVERIFY(familyChain(QStringLiteral("vp9")).isEmpty());
        QVERIFY(familyChain(QStringLiteral("prores")).isEmpty());
    }

    void chainStartsAtThePickAndEndsOnTheCpu()
    {
        const QStringList chain = fallbackChain(QStringLiteral("h264_nvenc"), {});
        QCOMPARE(chain.first(), QStringLiteral("h264_nvenc"));
        QCOMPARE(chain.at(1), QStringLiteral("h264_amf"));
        QCOMPARE(chain.at(2), QStringLiteral("h264_qsv"));
        QCOMPARE(chain.at(3), QStringLiteral("h264_vaapi"));
        QCOMPARE(chain.last(), QStringLiteral("h264"));
        QCOMPARE(chain.count(QStringLiteral("h264")), 1);

        // Picking AMF never goes back up to NVENC.
        const QStringList amf = fallbackChain(QStringLiteral("h264_amf"), {});
        QCOMPARE(amf.first(), QStringLiteral("h264_amf"));
        QVERIFY(!amf.contains(QStringLiteral("h264_nvenc")));
        QCOMPARE(amf.last(), QStringLiteral("h264"));
    }

    void chainSkipsKnownBrokenEncoders()
    {
        const QSet<QString> broken{QStringLiteral("h264_nvenc"), QStringLiteral("h264_qsv"),
                                   QStringLiteral("h264")};
        const QStringList chain = fallbackChain(QStringLiteral("h264_nvenc"), broken);
        QCOMPARE(chain.first(), QStringLiteral("h264_amf"));
        QVERIFY(!chain.contains(QStringLiteral("h264_qsv")));
        // The software encoder is the last resort even if something marked it.
        QCOMPARE(chain.last(), QStringLiteral("h264"));
    }

    void softwarePickStaysOnSoftware()
    {
        QCOMPARE(fallbackChain(QStringLiteral("h264"), {}), QStringList{QStringLiteral("h264")});
        QCOMPARE(fallbackChain(QStringLiteral("vp9"), {}), QStringList{QStringLiteral("vp9")});
    }

    void bestGpuEncoderUsesTheFirstWorkingOne()
    {
        const auto all = [](const QString &) { return true; };
        QCOMPARE(bestGpuEncoder(QStringLiteral("h264"), {}, all), QStringLiteral("h264_nvenc"));
        QCOMPARE(bestGpuEncoder(QStringLiteral("h264"), {QStringLiteral("h264_nvenc")}, all),
                 QStringLiteral("h264_amf"));
        const auto intelOnly = [](const QString &id) { return id.endsWith(QLatin1String("_qsv")); };
        QCOMPARE(bestGpuEncoder(QStringLiteral("h265"), {}, intelOnly), QStringLiteral("h265_qsv"));
        QVERIFY(bestGpuEncoder(QStringLiteral("h264"), {}, [](const QString &) { return false; }).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestHwEncodeFallback)
#include "tst_hwencodefallback.moc"
