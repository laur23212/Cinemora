#include "app/StartupGuard.h"

#include "core/base/SafeMode.h"

#include <QSettings>

#include <cstring>

namespace drift::startup {

bool begin(int argc, char *argv[])
{
    bool flagGiven = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--safe-mode") == 0)
            flagGiven = true;
    }

    QSettings settings;
    const int unfinished = settings.value(QLatin1String(safemode::kAttemptsKey), 0).toInt();
    const bool safe = safemode::shouldEnter(unfinished, flagGiven);
    safemode::setActive(safe);
    if (safe)
        qWarning("Starting in safe mode (%s)", flagGiven ? "--safe-mode" : "two start-ups in a row failed");

    // Synced now: a crash during startup must still find the count on the next launch.
    settings.setValue(QLatin1String(safemode::kAttemptsKey), unfinished + 1);
    settings.sync();
    return safe;
}

void markStarted()
{
    QSettings settings;
    settings.setValue(QLatin1String(safemode::kAttemptsKey), 0);
    settings.sync();
}

} // namespace drift::startup
