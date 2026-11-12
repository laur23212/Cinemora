#include "app/platform/OsNotifier.h"

#include <QApplication>
#include <QIcon>
#include <QMenu>
#if QT_CONFIG(process)
#include <QProcess>
#endif
#include <QStandardPaths>
#if QT_CONFIG(systemtrayicon)
#include <QSystemTrayIcon>
#endif
#include <QTimer>

namespace {
// Long enough to read the toast and click it from the notification centre.
constexpr int kTrayLingerMs = 2 * 60 * 1000;
} // namespace

OsNotifier::OsNotifier(QObject *parent)
    : QObject(parent)
{
}

void OsNotifier::notify(const QString &title, const QString &body, bool ok, std::function<void()> action)
{
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID) && QT_CONFIG(process)
    if (notifyWithNotifySend(title, body, ok, action))
        return;
#endif
    notifyWithTray(title, body, ok, std::move(action));
}

bool OsNotifier::notifyWithNotifySend(const QString &title, const QString &body, bool ok,
                                      const std::function<void()> &action)
{
#if !QT_CONFIG(process)
    Q_UNUSED(title);
    Q_UNUSED(body);
    Q_UNUSED(ok);
    Q_UNUSED(action);
    return false;
#else
    const QString program = QStandardPaths::findExecutable(QStringLiteral("notify-send"));
    if (program.isEmpty())
        return false;

    // --action makes notify-send wait and print the chosen action's key (libnotify 0.7.9+). It
    // is never run through a shell; the title and body are plain arguments.
    auto *process = new QProcess(this);
    const QStringList baseArgs = {QStringLiteral("--app-name=Dafin"),
                                  QStringLiteral("--icon=") + (ok ? QStringLiteral("video-x-generic")
                                                                  : QStringLiteral("dialog-error")),
                                  QStringLiteral("--urgency=") + (ok ? QStringLiteral("normal")
                                                                     : QStringLiteral("critical"))};
    QStringList args = baseArgs;
    args << QStringLiteral("--action=open=") + tr("Open folder") << title << body;

    connect(process, &QProcess::finished, this,
            [this, process, program, baseArgs, title, body, action](int exitCode, QProcess::ExitStatus) {
                const QString chosen = QString::fromUtf8(process->readAllStandardOutput()).trimmed();
                const QString err = QString::fromUtf8(process->readAllStandardError());
                process->deleteLater();
                if (chosen == QLatin1String("open")) {
                    if (action)
                        action();
                    return;
                }
                // An older notify-send without --action: show it plainly (no button).
                if (exitCode != 0 && err.contains(QLatin1String("action"), Qt::CaseInsensitive))
                    QProcess::startDetached(program, QStringList(baseArgs) << title << body);
            });
    connect(process, &QProcess::errorOccurred, this, [process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            process->deleteLater();
    });
    process->start(program, args);
    return true;
#endif
}

void OsNotifier::notifyWithTray(const QString &title, const QString &body, bool ok, std::function<void()> action)
{
#if QT_CONFIG(systemtrayicon)
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    if (!m_tray) {
        m_tray = new QSystemTrayIcon(QApplication::windowIcon(), this);
        m_tray->setToolTip(QStringLiteral("Dafin"));
        auto *menu = new QMenu();
        connect(this, &QObject::destroyed, menu, &QObject::deleteLater);
        menu->addAction(tr("Open folder"), this, [this] {
            if (m_trayAction)
                m_trayAction();
        });
        menu->addAction(tr("Dismiss"), this, [this] { m_tray->hide(); });
        m_tray->setContextMenu(menu);
        const auto run = [this] {
            if (m_trayAction)
                m_trayAction();
            m_tray->hide();
        };
        connect(m_tray, &QSystemTrayIcon::messageClicked, this, run);
        connect(m_tray, &QSystemTrayIcon::activated, this, [run](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
                run();
        });
        m_hideTimer = new QTimer(this);
        m_hideTimer->setSingleShot(true);
        connect(m_hideTimer, &QTimer::timeout, m_tray, &QSystemTrayIcon::hide);
    }
    m_trayAction = std::move(action);
    m_tray->show();
    m_tray->showMessage(title, body + QLatin1Char('\n') + tr("Click to open the folder."),
                        ok ? QSystemTrayIcon::Information : QSystemTrayIcon::Warning, 10000);
    m_hideTimer->start(kTrayLingerMs);
#else
    Q_UNUSED(title);
    Q_UNUSED(body);
    Q_UNUSED(ok);
    Q_UNUSED(action);
#endif
}
