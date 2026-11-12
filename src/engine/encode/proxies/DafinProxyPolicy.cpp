#include "engine/encode/proxies/DafinProxyPolicy.h"

#include "engine/encode/proxies/PreviewProxyRenderer.h"
#include "engine/encode/proxies/ReverseProxyCache.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QSettings>

#include <algorithm>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}

namespace dafin::proxy {

namespace {

QMutex s_mutex;
QSet<QString> s_failedEncoders;
QString s_lastUsed;
bool s_lastUsedSet = false;

QString settingsKey(const char *name)
{
    return QStringLiteral("dafin/") + QLatin1String(name);
}

QString tr(const char *text)
{
    return QCoreApplication::translate("DafinProxy", text);
}

} // namespace

QStringList heavyReasons(const MediaInfo &info, qint64 fileBytes)
{
    QStringList reasons;
    for (const StreamInfo &stream : info.streams) {
        if (stream.type != StreamInfo::Type::Video || stream.attachedPicture)
            continue;
        // The proxy is opaque; a transparent clip must keep its alpha.
        if (stream.hasAlpha)
            return {};
        const int shortSide = std::min(stream.width, stream.height);
        if (shortSide >= 2160)
            reasons << QStringLiteral("4K");
        else if (shortSide > 1080)
            reasons << QStringLiteral("%1p").arg(shortSide);
        if (stream.codecName == QLatin1String("hevc"))
            reasons << QStringLiteral("HEVC");
        if (stream.bitDepth > 8)
            reasons << QStringLiteral("%1-bit").arg(stream.bitDepth);
        const qint64 durationUs = info.durationUs > 0 ? info.durationUs : stream.durationUs;
        if (fileBytes > 0 && durationUs > 1'000'000) {
            const qint64 bitsPerSecond = qint64(double(fileBytes) * 8.0 * 1e6 / double(durationUs));
            if (bitsPerSecond > kHighBitrate)
                reasons << tr("High bitrate (%1 Mbit/s)").arg(bitsPerSecond / 1'000'000);
        }
        // The first real video stream decides, as in Drift's wantsPreviewProxy.
        break;
    }
    return reasons;
}

QString encoderPreference()
{
    const QString value = QSettings().value(settingsKey("proxyEncoder"), QStringLiteral("auto")).toString();
    static const QStringList known = {QStringLiteral("auto"), QStringLiteral("nvidia"),
                                      QStringLiteral("amd"), QStringLiteral("intel"),
                                      QStringLiteral("cpu")};
    return known.contains(value) ? value : QStringLiteral("auto");
}

void setEncoderPreference(const QString &preference)
{
    QSettings().setValue(settingsKey("proxyEncoder"), preference);
}

QStringList encoderCandidates(const QString &preference, bool windows)
{
    const QString nvidia = QStringLiteral("h264_nvenc");
    const QString amd = QStringLiteral("h264_amf");
    const QString intel = QStringLiteral("h264_qsv");
    QStringList gpu;
    if (preference == QLatin1String("nvidia"))
        gpu = {nvidia};
    else if (preference == QLatin1String("amd"))
        gpu = {amd};
    else if (preference == QLatin1String("intel"))
        gpu = {intel};
    else if (preference != QLatin1String("cpu")) {
        // AMF is Windows-only in FFmpeg; on Linux the AMD route is VAAPI, which needs GPU frames
        // this encoder doesn't make, so Linux AMD falls to the CPU.
        gpu = windows ? QStringList{nvidia, amd, intel} : QStringList{nvidia, intel};
    }
    gpu << QString(); // the CPU, always last
    return gpu;
}

QStringList encoderCandidates()
{
#ifdef Q_OS_WIN
    const bool windows = true;
#else
    const bool windows = false;
#endif
#ifdef Q_OS_ANDROID
    return {QString()};
#else
    return encoderCandidates(encoderPreference(), windows);
#endif
}

QString encoderLabel(const QString &encoderName)
{
    if (encoderName.contains(QLatin1String("nvenc")))
        return QStringLiteral("NVIDIA (%1)").arg(encoderName);
    if (encoderName.contains(QLatin1String("amf")))
        return QStringLiteral("AMD (%1)").arg(encoderName);
    if (encoderName.contains(QLatin1String("qsv")))
        return QStringLiteral("Intel (%1)").arg(encoderName);
    return tr("CPU (libx264)");
}

QString lastUsedEncoder()
{
    QMutexLocker lock(&s_mutex);
    return s_lastUsedSet ? encoderLabel(s_lastUsed) : QString();
}

void resetFailedEncoders()
{
    QMutexLocker lock(&s_mutex);
    s_failedEncoders.clear();
}

void applyHardwareEncoderOptions(AVCodecContext *ctx, const char *encoderName, int quality)
{
    if (!ctx || !encoderName)
        return;
    const QByteArray name(encoderName);
    void *priv = ctx->priv_data;
    ctx->bit_rate = 0;
    // Same rate-control mapping as the exporter (applyHwRateControl), tuned for speed.
    if (name.contains("nvenc")) {
        av_opt_set(priv, "preset", "p1", 0);
        av_opt_set(priv, "rc", "vbr", 0);
        av_opt_set_int(priv, "cq", quality, 0);
    } else if (name.contains("amf")) {
        av_opt_set(priv, "quality", "speed", 0);
        av_opt_set(priv, "rc", "cqp", 0);
        av_opt_set_int(priv, "qp_i", quality, 0);
        av_opt_set_int(priv, "qp_p", quality, 0);
        av_opt_set_int(priv, "qp_b", quality, 0);
    } else if (name.contains("qsv")) {
        av_opt_set(priv, "preset", "veryfast", 0);
        ctx->global_quality = quality;
    }
}

bool renderWithFallback(const QString &sourcePath, int shortSide, const QString &outPath,
                        QString *errorOut, const std::function<bool(double)> &onProgress,
                        QString *usedEncoderOut)
{
    bool cancelled = false;
    const auto progress = [&](double fraction) {
        const bool keepGoing = !onProgress || onProgress(fraction);
        if (!keepGoing)
            cancelled = true;
        return keepGoing;
    };

    QString firstError;
    for (const QString &encoder : encoderCandidates()) {
        if (!encoder.isEmpty()) {
            QMutexLocker lock(&s_mutex);
            if (s_failedEncoders.contains(encoder))
                continue;
            // Not built into this FFmpeg: nothing to try.
            if (!avcodec_find_encoder_by_name(encoder.toLatin1().constData())) {
                s_failedEncoders.insert(encoder);
                continue;
            }
        }
        QString error;
        const QByteArray name = encoder.toLatin1();
        const bool ok = drift::renderPreviewProxy(sourcePath, shortSide, outPath, &error, progress,
                                                  encoder.isEmpty() ? nullptr : name.constData());
        if (ok) {
            QMutexLocker lock(&s_mutex);
            s_lastUsed = encoder;
            s_lastUsedSet = true;
            if (usedEncoderOut)
                *usedEncoderOut = encoder;
            return true;
        }
        if (cancelled) {
            if (errorOut)
                *errorOut = error;
            return false;
        }
        if (encoder.isEmpty()) {
            if (errorOut)
                *errorOut = error.isEmpty() ? firstError : error;
            return false;
        }
        // A GPU encoder failed: remember it for this session and try the next one.
        qWarning("Dafin proxy: %s failed (%s), trying the next encoder", name.constData(),
                 qPrintable(error));
        if (firstError.isEmpty())
            firstError = error;
        QMutexLocker lock(&s_mutex);
        s_failedEncoders.insert(encoder);
    }
    if (errorOut)
        *errorOut = firstError;
    return false;
}

bool besideSource()
{
    return QSettings().value(settingsKey("proxyBesideSource"), false).toBool();
}

QList<int> cacheLimitChoicesGb()
{
    return {2, 5, 10, 20, 50};
}

int cacheLimitGb()
{
#ifdef Q_OS_ANDROID
    const int fallback = 1;
#else
    const int fallback = 10;
#endif
    const int gb = QSettings().value(settingsKey("proxyCacheGb"), fallback).toInt();
    return gb > 0 ? gb : fallback;
}

void setCacheLimitGb(int gb)
{
    if (gb > 0)
        QSettings().setValue(settingsKey("proxyCacheGb"), gb);
}

qint64 cacheLimitBytes()
{
    return qint64(cacheLimitGb()) * 1024 * 1024 * 1024;
}

qint64 cacheFolderBytes()
{
    const QString dir = drift::reverseCacheDir();
    if (dir.isEmpty())
        return 0;
    qint64 total = 0;
    QDirIterator it(dir, {QStringLiteral("*.mp4")}, QDir::Files);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

QString formatBytes(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

bool projectEnabled(const QString &projectId)
{
    if (projectId.isEmpty())
        return true;
    return QSettings().value(settingsKey("proxyProjects/") + projectId, true).toBool();
}

void setProjectEnabled(const QString &projectId, bool enabled)
{
    if (projectId.isEmpty())
        return;
    QSettings settings;
    const QString key = settingsKey("proxyProjects/") + projectId;
    if (enabled)
        settings.remove(key); // on is the default; only "off" needs remembering
    else
        settings.setValue(key, false);
}

QString importChoice()
{
    const QString value = QSettings().value(settingsKey("proxyImportChoice"), QStringLiteral("ask")).toString();
    return value == QLatin1String("proxy") || value == QLatin1String("full") ? value
                                                                              : QStringLiteral("ask");
}

void setImportChoice(const QString &choice)
{
    QSettings().setValue(settingsKey("proxyImportChoice"), choice);
}

} // namespace dafin::proxy
