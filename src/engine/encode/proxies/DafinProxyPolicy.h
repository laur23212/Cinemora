#pragma once

#include "engine/decode/probe/MediaProbe.h"

#include <QString>
#include <QStringList>

#include <functional>

struct AVCodecContext;

// Dafin proxy media: which clips are heavy enough to deserve a proxy, which encoder makes it
// (NVIDIA, AMD, Intel, then the CPU), and the proxy cache's settings. Drift's own proxy system
// (AssetLibrary, PreviewProxyRenderer, ReverseProxyCache) does the work; this only adds the rules.
namespace dafin::proxy {

// Why a clip is heavy to preview: "4K", "HEVC", "10-bit", "High bitrate (85 Mbit/s)". Empty when
// it is not, or when it has transparency (a proxy is opaque yuv420p). fileBytes is the file's
// size, used for the overall bitrate; 0 skips that check.
QStringList heavyReasons(const MediaInfo &info, qint64 fileBytes);
inline bool isHeavyVideo(const MediaInfo &info, qint64 fileBytes)
{
    return !heavyReasons(info, fileBytes).isEmpty();
}

// Overall bitrate above which a clip counts as heavy, in bits per second.
constexpr qint64 kHighBitrate = 50'000'000;

// "auto", "nvidia", "amd", "intel" or "cpu". QSettings "dafin/proxyEncoder".
QString encoderPreference();
void setEncoderPreference(const QString &preference);

// FFmpeg encoder names to try in order. The last one is always "" (the CPU, libx264), so a
// failing GPU encoder always falls back. Pure: `windows` says which hardware encoders exist.
QStringList encoderCandidates(const QString &preference, bool windows);
QStringList encoderCandidates(); // for this platform and the saved preference

// "NVIDIA (h264_nvenc)", "CPU (libx264)"...
QString encoderLabel(const QString &encoderName);
// The encoder that made the last proxy this session, or empty.
QString lastUsedEncoder();
// A GPU encoder that failed is not tried again in this session. Tests reset it.
void resetFailedEncoders();

// Rate control and speed options for a hardware H.264 encoder (nvenc, amf, qsv). `quality` is a
// CRF-like number (23 = proxy quality).
void applyHardwareEncoderOptions(AVCodecContext *ctx, const char *encoderName, int quality);

// renderPreviewProxy, tried with each encoder of encoderCandidates() until one works. A GPU encoder
// that fails (driver missing, out of sessions, rejects a frame) is followed by the next one, the
// CPU last. Stops at once on cancel. usedEncoderOut receives the encoder name ("" = CPU).
bool renderWithFallback(const QString &sourcePath, int shortSide, const QString &outPath,
                        QString *errorOut, const std::function<bool(double)> &onProgress,
                        QString *usedEncoderOut = nullptr);

// Dafin keeps its proxies in the proxy cache folder, not beside the media, so the cache cap and
// "Clear proxy cache" cover them. QSettings "dafin/proxyBesideSource" (default false) brings
// Drift's behaviour back.
bool besideSource();

// The proxy cache's size cap. QSettings "dafin/proxyCacheGb"; one of cacheLimitChoicesGb().
QList<int> cacheLimitChoicesGb();
int cacheLimitGb();
void setCacheLimitGb(int gb);
qint64 cacheLimitBytes();

// "Clear proxy cache" and the size shown beside it: every file in the proxy cache folder.
qint64 cacheFolderBytes();
QString formatBytes(qint64 bytes);

// Per-project switch, kept per project id in QSettings ("dafin/proxyProjects/<id>"), default on.
bool projectEnabled(const QString &projectId);
void setProjectEnabled(const QString &projectId, bool enabled);

// What to do after importing heavy clips: "ask" (default), "proxy" or "full".
QString importChoice();
void setImportChoice(const QString &choice);

} // namespace dafin::proxy
