#pragma once

#include <QMap>
#include <QStringList>
#include <QObject>
#include <QRect>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

// Panel arrangements the desktop editor can switch between: a split tree per window
// whose leaves hold panel types. Built-in presets ship in qrc and are never edited in
// place — a customised preset is stored as an override sharing its id, so "reset" is
// just deleting the override.
//
// App-wide (QSettings) for the same reason as LayoutStore: a layout belongs to the
// displays the user is at, not to the project.
class WorkspaceStore : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList workspaces READ workspaces NOTIFY workspacesChanged)
    Q_PROPERTY(QString activeId READ activeId WRITE setActiveId NOTIFY activeIdChanged)
    Q_PROPERTY(QVariantList panelTypes READ panelTypes CONSTANT)

public:
    explicit WorkspaceStore(QObject *parent = nullptr);

    QVariantList workspaces() const;
    QString activeId() const { return m_activeId; }
    void setActiveId(const QString &id);
    QVariantList panelTypes() const;

    Q_INVOKABLE QVariantMap workspace(const QString &id) const;
    Q_INVOKABLE QString workspaceWithPanel(const QString &type) const;
    Q_INVOKABLE QString validate(const QVariantMap &ws) const;
    Q_INVOKABLE QString saveWorkspace(const QVariantMap &ws);
    Q_INVOKABLE QString duplicate(const QString &id);
    Q_INVOKABLE bool remove(const QString &id);
    Q_INVOKABLE void resetPreset(const QString &id);
    Q_INVOKABLE void rename(const QString &id, const QString &name);

    // Live write-backs while a handle is dragged or a window is moved. They persist
    // without notifying, because the layout on screen already matches and rebuilding
    // it would tear the panels down mid-drag.
    Q_INVOKABLE void setSizes(const QString &id, const QString &windowId,
                              const QVariantList &nodePath, const QVariantList &sizes);
    // Dafin: the built-in sizes of one split (empty when the workspace is not a built-in or the
    // path is wrong), used when a divider is double-clicked to put it back.
    Q_INVOKABLE QVariantList presetSizes(const QString &id, const QString &windowId,
                                         const QVariantList &nodePath) const;
    // Dafin: pulls each divider onto the nearest of 1/4, 1/3, 1/2, 2/3 or 3/4 when it is
    // within 2 % of one. Sizes that would leave a section under 15 % come back unchanged.
    Q_INVOKABLE QVariantList snapSizes(const QVariantList &sizes) const;
    Q_INVOKABLE void setWindowGeometry(const QString &id, const QString &windowId,
                                       const QRect &geometry, const QString &screenName);

signals:
    void workspacesChanged();
    void activeIdChanged();
    // The active workspace's structure changed, so the host has to rebuild. Not emitted
    // for size or geometry write-backs.
    void activeWorkspaceEdited();

private:
    bool exists(const QString &id) const;
    int userIndex(const QString &id) const;
    QVariantMap effective(const QString &id) const;
    QString newId() const;
    void store(const QVariantMap &ws);
    void persist() const;
    void loadPresets();
    void loadUser();
    void migrateOldFractions();
    void migrateCutTrim();
    void migrateSourceLabBin();
    void fixActive();
    bool editQuietly(const QString &id, const std::function<bool(QVariantMap &)> &edit);

    QMap<QString, QVariantMap> m_presets;
    QStringList m_presetOrder;
    QVariantList m_user;
    QString m_activeId;
};
