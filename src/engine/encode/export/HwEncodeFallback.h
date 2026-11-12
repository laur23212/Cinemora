#pragma once

#include <QString>

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

} // namespace drift::hwencode
