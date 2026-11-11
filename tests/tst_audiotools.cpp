// Dafin audio tools: the FFmpeg chains behind Loudness, Noise reduction and Silence remover.
#include "engine/audio/dsp/FfmpegAudioFilter.h"

#include <QtTest>

#include <cmath>
#include <random>
#include <vector>

namespace {

constexpr int kRate = 48000;
constexpr double kPi = 3.14159265358979323846; // M_PI is not defined by default on MSVC

// Stereo: `seconds` of a 440 Hz tone at `amplitude` (0 = digital silence).
void appendTone(std::vector<float> &buf, double seconds, double amplitude)
{
    const int frames = int(seconds * kRate);
    const size_t base = buf.size() / 2;
    for (int i = 0; i < frames; ++i) {
        const float v = float(amplitude * std::sin(2.0 * kPi * 440.0 * double(base + i) / kRate));
        buf.push_back(v);
        buf.push_back(v);
    }
}

double rms(const std::vector<float> &buf, size_t from = 0, size_t to = 0)
{
    if (to == 0 || to > buf.size())
        to = buf.size();
    double sum = 0.0;
    for (size_t i = from; i < to; ++i)
        sum += double(buf[i]) * buf[i];
    return std::sqrt(sum / double(std::max<size_t>(1, to - from)));
}

} // namespace

class TestAudioTools : public QObject
{
    Q_OBJECT

private slots:
    void filtersExist()
    {
        QVERIFY(drift::FfmpegAudioFilter::hasFilter("loudnorm"));
        QVERIFY(drift::FfmpegAudioFilter::hasFilter("afftdn"));
        QVERIFY(drift::FfmpegAudioFilter::hasFilter("silencedetect"));
        QVERIFY(drift::FfmpegAudioFilter::hasFilter("ebur128"));
    }

    void passThroughKeepsLength()
    {
        std::vector<float> in;
        appendTone(in, 1.3, 0.3);
        QString error;
        const std::vector<float> out = drift::runAudioFilter(in, kRate, 2, QStringLiteral("anull"), &error);
        QVERIFY2(!out.empty(), qPrintable(error));
        QCOMPARE(out.size(), in.size());
        QVERIFY(std::abs(rms(out) - rms(in)) < 1e-3);
    }

    void badChainReportsError()
    {
        std::vector<float> in;
        appendTone(in, 0.5, 0.3);
        QString error;
        QVERIFY(drift::runAudioFilter(in, kRate, 2, QStringLiteral("nosuchfilter"), &error).empty());
        QVERIFY(!error.isEmpty());
    }

    void silenceFoundBetweenTones()
    {
        std::vector<float> in;
        appendTone(in, 1.0, 0.3);
        appendTone(in, 1.5, 0.0); // 1.0 – 2.5 s
        appendTone(in, 1.0, 0.3);
        appendTone(in, 0.2, 0.0); // too short at d=0.5
        appendTone(in, 0.5, 0.3);
        appendTone(in, 0.8, 0.0); // runs to the end: 4.2 – 5.0 s
        QString error;
        const auto spans = drift::detectSilenceFfmpeg(in, kRate, 2, -40.0, 0.5, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(spans.size(), 2);
        QVERIFY(std::abs(spans.at(0).start - 1.0) < 0.02);
        QVERIFY(std::abs(spans.at(0).end - 2.5) < 0.02);
        QVERIFY(std::abs(spans.at(1).start - 4.2) < 0.02);
        QVERIFY(std::abs(spans.at(1).end - 5.0) < 0.02);
    }

    void thresholdDecidesWhatCountsAsSilence()
    {
        std::vector<float> in;
        appendTone(in, 1.0, 0.3);
        appendTone(in, 1.0, 0.003); // about -53 dBFS peak
        appendTone(in, 1.0, 0.3);
        QString error;
        QCOMPARE(drift::detectSilenceFfmpeg(in, kRate, 2, -40.0, 0.5, &error).size(), 1);
        QCOMPARE(drift::detectSilenceFfmpeg(in, kRate, 2, -60.0, 0.5, &error).size(), 0);
    }

    void loudnormReachesTarget()
    {
        std::vector<float> in;
        appendTone(in, 6.0, 0.05); // quiet: roughly -29 LUFS
        QString error;
        const drift::LoudnessStats before = drift::measureEbuR128(in, kRate, 2, &error);
        QVERIFY2(before.ok, qPrintable(error));
        QVERIFY(before.integratedLufs < -24.0);

        const QString chain = drift::loudnormChain(-14.0, -1.0, 11.0, before);
        const std::vector<float> out = drift::runAudioFilter(in, kRate, 2, chain, &error);
        QVERIFY2(!out.empty(), qPrintable(error));
        QCOMPARE(out.size(), in.size());
        const drift::LoudnessStats after = drift::measureEbuR128(out, kRate, 2, &error);
        QVERIFY(after.ok);
        QVERIFY2(std::abs(after.integratedLufs + 14.0) < 1.0,
                 qPrintable(QString::number(after.integratedLufs)));
        QVERIFY(after.truePeakDb <= -0.5);
    }

    void silentClipIsNotMeasured()
    {
        std::vector<float> in;
        appendTone(in, 2.0, 0.0);
        QString error;
        QVERIFY(!drift::measureEbuR128(in, kRate, 2, &error).ok);
        QVERIFY(!error.isEmpty());
    }

    void afftdnLowersNoise()
    {
        std::mt19937 rng(7);
        std::normal_distribution<float> noise(0.0f, 0.01f);
        std::vector<float> in;
        appendTone(in, 4.0, 0.0);
        for (float &s : in)
            s = noise(rng);
        QString error;
        const std::vector<float> weak = drift::runAudioFilter(in, kRate, 2, drift::afftdnChain(0.1), &error);
        const std::vector<float> strong = drift::runAudioFilter(in, kRate, 2, drift::afftdnChain(1.0), &error);
        QVERIFY2(!weak.empty() && !strong.empty(), qPrintable(error));
        // Judged after the first second, once the noise floor has been tracked.
        const size_t from = size_t(kRate) * 2;
        QVERIFY(rms(weak, from) < rms(in, from));
        QVERIFY(rms(strong, from) < rms(weak, from));
    }
};

QTEST_GUILESS_MAIN(TestAudioTools)
#include "tst_audiotools.moc"
