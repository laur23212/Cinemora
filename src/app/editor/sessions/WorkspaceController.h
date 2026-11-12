#pragma once

#include "core/edit/commands/ProjectCommands.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantList>

class AppController;

namespace drift {
class Project;
}

// Dafin: workspaces, separate timelines in one project (the Workspace menu in the top bar).
//
// Each workspace has its own main timeline, bookmarks, work area, playhead, selection, zoom and
// undo history; media, composites and project settings are shared (Project::workspaces). The
// open workspace's timeline is the project's own tracks, so every edit path works unchanged;
// switching stores it into its entry and loads the other one, the way a composite tab does.
// The other workspaces' undo lists wait here, as immutable history states, until they are
// opened again; only the open workspace's list is saved with the project.
//
// Queuing an export keeps the queued timeline as "Render timeline N" (addRenderTimeline). The
// render itself works from its own frozen snapshot, so editing or closing the workspace never
// touches it.
//
// Reached from QML as EditorState.workspaces.
class WorkspaceController : public QObject
{
    Q_OBJECT
    // [{id, name, active, unsaved, render, renderNumber}], in menu order.
    Q_PROPERTY(QVariantList workspaces READ workspaces NOTIFY changed)
    Q_PROPERTY(QString activeId READ activeId NOTIFY changed)
    Q_PROPERTY(QString activeName READ activeName NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    explicit WorkspaceController(AppController *app, QObject *parent = nullptr);

    QVariantList workspaces() const;
    QString activeId() const;
    QString activeName() const;
    int count() const;
    int revision() const { return m_revision; }

    Q_INVOKABLE bool exists(const QString &id) const;
    // Opens a workspace. False for an unknown id.
    Q_INVOKABLE bool switchTo(const QString &id);
    // A new workspace with an empty timeline, opened. Returns its id.
    Q_INVOKABLE QString addEmpty();
    // Edits since the project was last saved (a new render timeline counts as one).
    Q_INVOKABLE bool hasUnsavedChanges(const QString &id) const;
    // Closes without asking (the menu asks first). Never the last one; a render it was made for
    // keeps going. Opens a neighbour first when it is the open one.
    Q_INVOKABLE bool close(const QString &id);
    Q_INVOKABLE bool rename(const QString &id, const QString &name);

    // The export queue's hook: keeps `snapshot`'s main timeline (bookmarks and work area too) as
    // a new workspace named "Render timeline N", N counting every render queued in this project.
    // The open workspace stays open and unchanged. Returns the new workspace's id, for the
    // queue's "Open its timeline" link.
    QString addRenderTimeline(const drift::Project &snapshot);
    // The same, from the main timeline as it is now (the snapshot an export takes).
    Q_INVOKABLE QString addRenderTimelineFromOpen();

    // The timeline panel's zoom and scroll, recorded into the open workspace before a switch.
    Q_INVOKABLE void rememberView(double zoom, double scrollX);

    // A project was opened or a new one made: the stored undo lists belong to the old one.
    void forgetSession();

signals:
    void changed();
    // Before the open workspace is stored: the timeline panel answers with rememberView().
    void aboutToSwitch();
    // After a switch: the timeline panel shows the workspace at this zoom (0 = leave as is).
    void viewRestoreRequested(double zoom, double scrollX);

private:
    void ensureEntries();
    void storeView();
    void restoreView();
    QString nextTimelineName() const;
    void bump();

    AppController *m_app = nullptr;
    QHash<QString, drift::HistoryCapture> m_histories;
    QSet<QString> m_unsaved;
    // Set while this controller rebuilds the undo stack, so that is not taken for an edit.
    bool m_swapping = false;
    int m_revision = 0;
};
