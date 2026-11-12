#pragma once

#include <atomic>

// Power-user performance knobs, plain atomics so hot paths pay one relaxed load and never touch
// QSettings. PreferencesController owns persistence: it loads the stored values at startup and
// pushes every change here. The defaults are the engine's normal behaviour; 0 means "off",
// "unlimited" or "automatic" depending on the knob (see each comment).
namespace drift::tuning {

struct Defaults
{
    static constexpr int staleFrameLagMs = 0;
    static constexpr int readAheadMs = 2000;
    static constexpr bool continuousReadAhead = true;
    static constexpr int targetPoolMax = 32;
    static constexpr int targetPoolMiB = 0;
    static constexpr int uploadCacheMax = 48;
    static constexpr int uploadCacheMiB = 0;
    static constexpr int readerHistoryMiB = 0;
    static constexpr int frameCacheMiB = 0;
    static constexpr bool preferHardwareExport = false;
    // Dafin: a failed GPU export runs once more on the matching CPU encoder (HwEncodeFallback.h).
    static constexpr bool cpuFallbackOnGpuEncodeFailure = true;
};

// Preview frames that finish this far behind the playhead are dropped. 0 never drops for lateness.
inline std::atomic<int> staleFrameLagMs{Defaults::staleFrameLagMs};
// How much decoded source each clip's reader keeps buffered ahead of the playhead during
// playback. This absorbs frames that decode slower than realtime (long GOPs, a heavy transition)
// by spending the slack on either side of them. The byte budget in ClipReader, sized from physical
// memory, bounds it too. The 2 s default is deliberate: 5 s was measured slower after a seek (two
// 1080p30 software streams dropped from ~15 to ~12 fps) because the prefetch then competes with the
// frames playback actually needs. Every edit or seek discards the part of the buffer past the
// change. Too much eager decoding as an adjacent clip opens can make the outgoing clip crawl near
// a cut, which is what lowering it (or turning off continuous read-ahead) is for. Read per request,
// so changes apply to the next frame requested.
inline std::atomic<int> readAheadMs{Defaults::readAheadMs};
inline std::atomic<bool> continuousReadAhead{Defaults::continuousReadAhead};
inline std::atomic<int> targetPoolMax{Defaults::targetPoolMax};
// 0 = no byte cap.
inline std::atomic<int> targetPoolMiB{Defaults::targetPoolMiB};
inline std::atomic<int> uploadCacheMax{Defaults::uploadCacheMax};
inline std::atomic<int> uploadCacheMiB{Defaults::uploadCacheMiB};
// CPU-side decoded history per reader. 0 = frame count only.
inline std::atomic<int> readerHistoryMiB{Defaults::readerHistoryMiB};
// Decoded preview frames across all readers. 0 = automatic from physical memory.
inline std::atomic<int> frameCacheMiB{Defaults::frameCacheMiB};
inline std::atomic<bool> preferHardwareExport{Defaults::preferHardwareExport};
inline std::atomic<bool> cpuFallbackOnGpuEncodeFailure{Defaults::cpuFallbackOnGpuEncodeFailure};

inline constexpr unsigned long long kMiB = 1024ull * 1024;

} // namespace drift::tuning
