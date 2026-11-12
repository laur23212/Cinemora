#pragma once

#include <QObject>
#include <QString>

#include <functional>

class QSystemTrayIcon;
class QTimer;

// Dafin: a desktop notification with an "Open folder" action.
//
// Linux: notify-send (libnotify) with an "Open folder" button; picking it runs `action`.
// Windows (and Linux without notify-send): a tray icon's balloon/toast; clicking the message,
// or "Open folder" in the tray icon's menu, runs `action`. The tray icon goes away after a while.
// Does nothing where the system has neither.
class OsNotifier : public QObject
{
    Q_OBJECT

public:
    explicit OsNotifier(QObject *parent = nullptr);

    void notify(const QString &title, const QString &body, bool ok, std::function<void()> action);

private:
    bool notifyWithNotifySend(const QString &title, const QString &body, bool ok,
                              const std::function<void()> &action);
    void notifyWithTray(const QString &title, const QString &body, bool ok, std::function<void()> action);

    QSystemTrayIcon *m_tray = nullptr;
    QTimer *m_hideTimer = nullptr;
    std::function<void()> m_trayAction;
};
