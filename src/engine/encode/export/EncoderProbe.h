#pragma once

#include <QSet>
#include <QString>

// Dafin: which GPU encoders really work on this machine.
//
// Exporter::videoCodecs() says a GPU encoder is "available" when its device opens. That is not the
// same as working: an old driver, a hybrid laptop on its integrated GPU or NVENC's session limit
// all pass the device check and then fail at avcodec_open2 or on the first frame. At first launch,
// and again whenever the GPUs, their driver versions or the FFmpeg build change, every available
// GPU encoder runs Exporter::testEncode() once on a background thread and the answers are kept in
// settings (export/encoderProbe/*). An encoder that fails a real export is added to the broken set
// too, so the next export skips it. The export chain itself is HwEncodeFallback.h.
//
// A driver that crashes the process inside a test encode leaves that encoder's name in a marker;
// the next start counts it as broken instead of crashing again. One that hangs blocks only the
// probe thread, and the same marker catches it on the next start.
namespace drift::encoderprobe {

// GPU names, PCI ids, kernel driver (Linux), driver versions and the FFmpeg version, in one string.
// A change in any of them means the cached answers are stale.
QString machineSignature();

// Starts the probe on a background thread when the cached answers are missing, incomplete or for
// another machine signature. At most once per process; returns at once.
void startBackgroundProbe();

// True once the cache describes this machine (the probe finished now or on an earlier start).
bool probeComplete();

// Encoders known not to work here: failed the test encode, crashed or hung in it, or failed a real
// export since. Empty before the first probe has finished anything.
QSet<QString> knownBroken();

// Encoders that passed the test encode.
QSet<QString> knownWorking();

// Records that `videoCodecId` failed a real export, with FFmpeg's reason, so the next export goes
// straight to the next encoder in the chain. Cleared with the rest when the signature changes.
void markBroken(const QString &videoCodecId, const QString &reason);

// Why `videoCodecId` is in knownBroken(), for the log and the debug report.
QString brokenReason(const QString &videoCodecId);

// Runs the probe on the calling thread; used by startBackgroundProbe() and the tests.
void runProbe();

} // namespace drift::encoderprobe
