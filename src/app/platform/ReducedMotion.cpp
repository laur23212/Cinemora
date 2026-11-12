#include "app/platform/ReducedMotion.h"

#include <QByteArray>
#include <QtGlobal>

#if defined(Q_OS_WIN)
#include <windows.h>
#elif defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#endif

namespace drift::platform {

bool systemPrefersReducedMotion()
{
    const QByteArray forced = qgetenv("DAFIN_REDUCE_MOTION").trimmed();
    if (!forced.isEmpty())
        return forced != "0" && forced.toLower() != "false";

#if defined(Q_OS_WIN)
    BOOL animations = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0))
        return animations == FALSE;
    return false;
#elif defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    // GNOME and most desktops built on its settings. A missing gsettings means "no preference".
    const QString gsettings = QStandardPaths::findExecutable(QStringLiteral("gsettings"));
    if (gsettings.isEmpty())
        return false;
    QProcess process;
    process.start(gsettings, {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.interface"),
                              QStringLiteral("enable-animations")});
    if (!process.waitForFinished(300) || process.exitStatus() != QProcess::NormalExit
        || process.exitCode() != 0)
        return false;
    return process.readAllStandardOutput().trimmed() == "false";
#else
    return false;
#endif
}

} // namespace drift::platform
