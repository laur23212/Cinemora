#include "engine/encode/export/HwEncodeFallback.h"

#include <QLatin1StringView>

namespace drift::hwencode {

namespace {

// Suffixes of the GPU rows in Exporter's codec table (kVideoCodecs).
constexpr QLatin1StringView kGpuSuffixes[] = {
    QLatin1StringView("_nvenc"),        QLatin1StringView("_qsv"),
    QLatin1StringView("_amf"),          QLatin1StringView("_vaapi"),
    QLatin1StringView("_videotoolbox"), QLatin1StringView("_mediacodec"),
};

// Codec family of a GPU id: "h264_nvenc" -> "h264". Empty when the id has no GPU suffix.
QString gpuFamily(const QString &id)
{
    for (QLatin1StringView suffix : kGpuSuffixes) {
        if (id.size() > suffix.size() && id.endsWith(suffix))
            return id.left(id.size() - suffix.size());
    }
    return {};
}

} // namespace

bool isGpuEncoderId(const QString &videoCodecId)
{
    return !softwareFallbackCodec(videoCodecId).isEmpty();
}

QString softwareFallbackCodec(const QString &videoCodecId)
{
    const QString family = gpuFamily(videoCodecId);
    if (family == QLatin1String("h264"))
        return QStringLiteral("h264");
    if (family == QLatin1String("h265"))
        return QStringLiteral("h265");
    if (family == QLatin1String("av1"))
        return QStringLiteral("av1_svt");
    return {};
}

bool isRetryPointless(const QString &error)
{
    if (error == QLatin1String("Export cancelled") || error == QLatin1String("Cancelled"))
        return true;
    // Exporter passes FFmpeg's own reason through (see Exporter.cpp, "FFmpeg's own reason").
    static const QLatin1StringView kSameOnCpu[] = {
        QLatin1StringView("No space left on device"),
        QLatin1StringView("Permission denied"),
        QLatin1StringView("Read-only file system"),
    };
    for (QLatin1StringView reason : kSameOnCpu) {
        if (error.contains(reason, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

bool shouldRetryOnCpu(bool fallbackEnabled, const QString &videoCodecId, bool encodeFailed,
                      const QString &error)
{
    return fallbackEnabled && encodeFailed && isGpuEncoderId(videoCodecId) && !isRetryPointless(error);
}

} // namespace drift::hwencode
