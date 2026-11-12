#include "engine/encode/export/EncoderProbe.h"

#include "engine/decode/readers/ClipReader.h"
#include "engine/encode/export/Exporter.h"
#include "engine/gpu/device/GpuDevice.h"

#include <QFile>
#include <QMutex>
#include <QSettings>
#include <QStringList>
#include <QVariantMap>

#include <atomic>
#include <thread>

extern "C" {
#include <libavutil/avutil.h>
}

namespace drift::encoderprobe {

namespace {

const QString kSignature = QStringLiteral("export/encoderProbe/signature");
const QString kComplete = QStringLiteral("export/encoderProbe/complete");
const QString kResults = QStringLiteral("export/encoderProbe/results");
const QString kInProgress = QStringLiteral("export/encoderProbe/inProgress");
const QString kOk = QStringLiteral("ok");

// Serialises read-modify-write of the results map between the probe thread and an export that
// marks an encoder broken.
QMutex g_lock;
std::atomic<bool> g_started{false};

QString firstLine(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readLine()).trimmed();
}

QStringList driverVersions()
{
    QStringList out;
#if defined(Q_OS_WIN)
    // Display adapters' class key: one subkey (0000, 0001…) per installed adapter, each with the
    // driver's DriverDesc and DriverVersion. Readable without elevation.
    QSettings display(QStringLiteral("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Class\\"
                                     "{4d36e968-e325-11ce-bfc1-08002be10318}"),
                      QSettings::NativeFormat);
    const QStringList keys = display.childGroups();
    for (const QString &key : keys) {
        const QString desc = display.value(key + QStringLiteral("/DriverDesc")).toString();
        const QString version = display.value(key + QStringLiteral("/DriverVersion")).toString();
        if (!desc.isEmpty() || !version.isEmpty())
            out.append(desc + QLatin1Char('=') + version);
    }
#elif defined(Q_OS_LINUX)
    // NVIDIA's proprietary driver names itself here; the kernel modules that version themselves
    // (nvidia, some amdgpu DKMS builds) do so in sysfs. Mesa updates show up through the FFmpeg/
    // libva side failing the next real export, which marks the encoder broken anyway.
    const QString nvidia = firstLine(QStringLiteral("/proc/driver/nvidia/version"));
    if (!nvidia.isEmpty())
        out.append(nvidia);
    for (const char *module : {"nvidia", "amdgpu", "i915", "xe"}) {
        const QString v = firstLine(QStringLiteral("/sys/module/%1/version").arg(QLatin1String(module)));
        if (!v.isEmpty())
            out.append(QLatin1String(module) + QLatin1Char('=') + v);
    }
    out.append(firstLine(QStringLiteral("/proc/sys/kernel/osrelease")));
#endif
    return out;
}

QVariantMap results(QSettings &s)
{
    return s.value(kResults).toMap();
}

} // namespace

QString machineSignature()
{
    QStringList parts;
    for (const drift::gpu::Adapter &a : drift::gpu::enumerateAdapters()) {
        parts.append(QStringLiteral("%1|%2:%3|%4")
                         .arg(a.name)
                         .arg(a.vendorId, 4, 16, QLatin1Char('0'))
                         .arg(a.deviceId, 4, 16, QLatin1Char('0'))
                         .arg(a.driver));
    }
    parts += driverVersions();
    parts.append(QString::fromUtf8(av_version_info()));
    return parts.join(QLatin1Char(';'));
}

bool probeComplete()
{
    QMutexLocker lock(&g_lock);
    QSettings s;
    return s.value(kComplete).toBool() && s.value(kSignature).toString() == machineSignature();
}

QSet<QString> knownBroken()
{
    QMutexLocker lock(&g_lock);
    QSettings s;
    QSet<QString> out;
    const QVariantMap map = results(s);
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        if (it.value().toString() != kOk)
            out.insert(it.key());
    }
    return out;
}

QSet<QString> knownWorking()
{
    QMutexLocker lock(&g_lock);
    QSettings s;
    QSet<QString> out;
    const QVariantMap map = results(s);
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        if (it.value().toString() == kOk)
            out.insert(it.key());
    }
    return out;
}

void markBroken(const QString &videoCodecId, const QString &reason)
{
    if (videoCodecId.isEmpty())
        return;
    QMutexLocker lock(&g_lock);
    QSettings s;
    QVariantMap map = results(s);
    map.insert(videoCodecId, reason.isEmpty() ? QStringLiteral("failed an export") : reason);
    s.setValue(kResults, map);
}

QString brokenReason(const QString &videoCodecId)
{
    QMutexLocker lock(&g_lock);
    QSettings s;
    const QString r = results(s).value(videoCodecId).toString();
    return r == kOk ? QString() : r;
}

void runProbe()
{
    const QString signature = machineSignature();
    {
        QMutexLocker lock(&g_lock);
        QSettings s;
        if (s.value(kSignature).toString() != signature) {
            // New GPU, new driver or new FFmpeg: every earlier answer, and every clip remembered as
            // needing software decode, may be wrong now.
            qInfo("EncoderProbe: GPU/driver changed, testing the GPU encoders again");
            s.setValue(kSignature, signature);
            s.setValue(kComplete, false);
            s.remove(kResults);
            s.remove(kInProgress);
            ClipReader::clearSoftwareOnlySources();
        } else if (s.value(kComplete).toBool()) {
            return;
        }

        // Still set from the last start: that test encode crashed the process or never returned.
        const QString stuck = s.value(kInProgress).toString();
        if (!stuck.isEmpty()) {
            QVariantMap map = results(s);
            map.insert(stuck, QStringLiteral("crashed or hung during the test encode"));
            s.setValue(kResults, map);
            s.remove(kInProgress);
            qWarning("EncoderProbe: %s did not finish its test encode last time; skipping it",
                     qUtf8Printable(stuck));
        }
    }

    QStringList candidates;
    for (const QVariant &v : Exporter::videoCodecs()) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("hardware")).toBool() && m.value(QStringLiteral("available")).toBool())
            candidates.append(m.value(QStringLiteral("id")).toString());
    }

    for (const QString &id : std::as_const(candidates)) {
        {
            QMutexLocker lock(&g_lock);
            QSettings s;
            if (results(s).contains(id))
                continue; // answered on an earlier, interrupted run
            s.setValue(kInProgress, id);
            s.sync(); // must be on disk before the driver gets a chance to take the process down
        }

        QString reason;
        const bool works = Exporter::testEncode(id, &reason);
        if (works)
            qInfo("EncoderProbe: %s works", qUtf8Printable(id));
        else
            qWarning("EncoderProbe: %s does not work here: %s", qUtf8Printable(id), qUtf8Printable(reason));

        QMutexLocker lock(&g_lock);
        QSettings s;
        QVariantMap map = results(s);
        // An export may have marked it broken while the test ran; a real failure wins.
        if (!(works && map.contains(id)))
            map.insert(id, works ? kOk : reason);
        s.setValue(kResults, map);
        s.remove(kInProgress);
    }

    QMutexLocker lock(&g_lock);
    QSettings s;
    if (s.value(kSignature).toString() == signature)
        s.setValue(kComplete, true);
}

void startBackgroundProbe()
{
    if (g_started.exchange(true))
        return;
    if (probeComplete())
        return;
    // Detached on purpose: a driver that hangs inside a test encode must not hold up quitting.
    std::thread([] { runProbe(); }).detach();
}

} // namespace drift::encoderprobe
