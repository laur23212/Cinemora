#pragma once

#include <QSet>
#include <QString>
#include <QStringList>

// Dafin: what an export does when a GPU encoder fails.
//
// Exporter::videoCodecById() only checks that the GPU *device* exists; the encoder can still refuse
// to open (old driver, a laptop running on its integrated GPU, every NVENC session in use) or stop
// part-way. Without this, the export just ends with an error. With "If the GPU encoder fails,
// finish on the CPU" on (Settings > Performance), the export is run once more on the matching
// software encoder. Pure string logic, so it is tested without a GPU (tests/tst_hwencodefallback).
namespace drift::hwencode {

// True for the export codec ids that run on a GPU (h264_nvenc, h265_amf, av1_qsv, ...).
bool isGpuEncoderId(const QString &videoCodecId);

// The software codec id that replaces a GPU one: H.264 -> "h264" (x264), H.265 -> "h265" (x265),
// AV1 -> "av1_svt". Empty for ids that are not GPU encoders.
QString softwareFallbackCodec(const QString &videoCodecId);

// Errors a second encode on the CPU would hit just the same: a cancel, a full disk, no write access.
bool isRetryPointless(const QString &error);

// Whether a failed export should be run again on the CPU. `encodeFailed` is true only when the
// encoder itself failed, not when the output target was rejected before encoding started.
bool shouldRetryOnCpu(bool fallbackEnabled, const QString &videoCodecId, bool encodeFailed,
                      const QString &error);

// Day 6: the full chain. GPU encoders of one family in the order Dafin tries them — NVIDIA, AMD,
// Intel QuickSync, VAAPI (Linux), then Apple/Android hardware — with the software encoder last.
// Empty for ids outside the H.264 / H.265 / AV1 families (VP9, ProRes…), which have no chain.
QStringList familyChain(const QString &videoCodecId);

// The encoders an export of `requested` tries, in order. A GPU pick starts with itself, then the
// GPU encoders after it in familyChain(), then the software one; ids in `knownBroken` (failed the
// test encode or a real export on this GPU/driver) are skipped, except the software encoder, which
// is always the last resort. A software pick is never moved onto a GPU: just {requested}.
QStringList fallbackChain(const QString &requested, const QSet<QString> &knownBroken);

// The first GPU encoder in `family`'s chain ("h264", "h265", "av1_svt" or any id of that family)
// that is not known broken and that `isAvailable` accepts (encoder in the build, device present).
// Empty when none is.
template <typename AvailableFn>
QString bestGpuEncoder(const QString &family, const QSet<QString> &knownBroken, AvailableFn isAvailable)
{
    for (const QString &id : familyChain(family)) {
        if (isGpuEncoderId(id) && !knownBroken.contains(id) && isAvailable(id))
            return id;
    }
    return {};
}

} // namespace drift::hwencode
