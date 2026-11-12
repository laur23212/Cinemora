#include "app/editor/sessions/WorkspaceController.h"

#include "app/editor/AppController.h"
#include "app/editor/ProjectFileController.h"
#include "app/mcp/McpController.h"
#include "core/project/Project.h"

#include <QSignalBlocker>
#include <QVariantMap>

WorkspaceController::WorkspaceController(AppController *app, QObject *parent)
    : QObject(parent), m_app(app)
{
    // An edit (or undo) in the open workspace leaves it with unsaved changes.
    connect(&m_app->m_undoStack, &QUndoStack::indexChanged, this, [this] {
        if (m_swapping)
            return;
        const QString id = activeId();
        if (!id.isEmpty() && !m_unsaved.contains(id)) {
            m_unsaved.insert(id);
            bump();
        }
    });
    // Saving writes every workspace, so none has unsaved changes afterwards.
    connect(m_app->m_projectFile, &ProjectFileController::dirtyChanged, this, [this] {
        if (!m_app->m_projectFile->hasUnsavedChanges())
            m_unsaved.clear();
        bump();
    });
}

void WorkspaceController::bump()
{
    ++m_revision;
    emit changed();
}

void WorkspaceController::forgetSession()
{
    m_histories.clear();
    m_unsaved.clear();
    bump();
}

void WorkspaceController::ensureEntries()
{
    m_app->m_project.ensureWorkspaces(tr("Main timeline"));
}

QVariantList WorkspaceController::workspaces() const
{
    const drift::Project &project = m_app->m_project;
    QVariantList list;
    if (project.workspaces().isEmpty()) {
        list.append(QVariantMap{{QStringLiteral("id"), QString()},
                                {QStringLiteral("name"), tr("Main timeline")},
                                {QStringLiteral("active"), true},
                                {QStringLiteral("unsaved"), false},
                                {QStringLiteral("render"), false},
                                {QStringLiteral("renderNumber"), 0}});
        return list;
    }
    for (const drift::TimelineWorkspace &workspace : project.workspaces()) {
        list.append(QVariantMap{{QStringLiteral("id"), workspace.id},
                                {QStringLiteral("name"), workspace.name},
                                {QStringLiteral("active"), workspace.id == project.activeWorkspaceId()},
                                {QStringLiteral("unsaved"), hasUnsavedChanges(workspace.id)},
                                {QStringLiteral("render"), workspace.renderNumber > 0},
                                {QStringLiteral("renderNumber"), workspace.renderNumber}});
    }
    return list;
}

QString WorkspaceController::activeId() const
{
    return m_app->m_project.activeWorkspaceId();
}

QString WorkspaceController::activeName() const
{
    const drift::Project &project = m_app->m_project;
    const int index = project.workspaceIndex(project.activeWorkspaceId());
    return index < 0 ? tr("Main timeline") : project.workspaces().at(index).name;
}

int WorkspaceController::count() const
{
    return qMax(1, int(m_app->m_project.workspaces().size()));
}

bool WorkspaceController::exists(const QString &id) const
{
    return !id.isEmpty() && m_app->m_project.workspaceIndex(id) >= 0;
}

bool WorkspaceController::hasUnsavedChanges(const QString &id) const
{
    // The project's own flag is the truth: opening a project clears the undo stack, which looks
    // like an edit here, and leaves the project clean.
    return m_unsaved.contains(id) && m_app->m_projectFile->hasUnsavedChanges();
}

QString WorkspaceController::nextTimelineName() const
{
    QSet<QString> taken;
    for (const drift::TimelineWorkspace &workspace : m_app->m_project.workspaces())
        taken.insert(workspace.name);
    for (int n = 2;; ++n) {
        const QString name = tr("Timeline %1").arg(n);
        if (!taken.contains(name))
            return name;
    }
}

void WorkspaceController::storeView()
{
    // The panel records zoom and scroll into the open entry (rememberView) from this signal.
    emit aboutToSwitch();
    drift::TimelineWorkspace *open = m_app->m_project.activeWorkspace();
    if (!open)
        return;
    open->playheadUs = m_app->m_playheadUs;
    open->selectedClipIds.clear();
    const QList<drift::Track> &tracks = m_app->m_project.tracks();
    for (const QPair<int, int> &pair : std::as_const(m_app->m_selection)) {
        if (pair.first >= 0 && pair.first < tracks.size() && pair.second >= 0
            && pair.second < tracks.at(pair.first).clips.size())
            open->selectedClipIds.append(tracks.at(pair.first).clips.at(pair.second).id);
    }
}

void WorkspaceController::rememberView(double zoom, double scrollX)
{
    if (drift::TimelineWorkspace *open = m_app->m_project.activeWorkspace()) {
        open->zoom = zoom;
        open->scrollX = scrollX;
    }
}

void WorkspaceController::restoreView()
{
    const drift::TimelineWorkspace *open = m_app->m_project.activeWorkspace();
    if (!open)
        return;
    m_app->setPlayheadUs(open->playheadUs);
    QList<QPair<int, int>> selection;
    const QList<drift::Track> &tracks = m_app->m_project.tracks();
    for (const QString &clipId : open->selectedClipIds) {
        for (int t = 0; t < tracks.size(); ++t) {
            for (int c = 0; c < tracks.at(t).clips.size(); ++c) {
                if (tracks.at(t).clips.at(c).id == clipId)
                    selection.append(qMakePair(t, c));
            }
        }
    }
    if (!selection.isEmpty()) {
        m_app->m_selection = selection;
        m_app->m_selectedTrack = selection.constLast().first;
        m_app->m_selectedClip = selection.constLast().second;
        m_app->notifySelectionChanged();
    }
    emit viewRestoreRequested(open->zoom, open->scrollX);
}

bool WorkspaceController::switchTo(const QString &id)
{
    drift::Project &project = m_app->m_project;
    if (id.isEmpty() || project.workspaceIndex(id) < 0)
        return false;
    if (id == project.activeWorkspaceId())
        return true;

    m_app->setPlaying(false);
    if (m_app->m_multicamActive)
        m_app->endMulticamSession();
    // Workspaces hold main timelines; a composite tab open in this one is left first.
    if (!project.activeSequenceId().isEmpty())
        m_app->openSequence(QString());
    if (!project.activeSequenceId().isEmpty())
        return false;

    storeView();
    const QString from = project.activeWorkspaceId();
    m_histories.insert(from, drift::captureHistory(m_app->m_undoStack));

    m_app->clearTransitionSelection();
    m_app->clearSelection();
    if (!project.activateWorkspace(id))
        return false;

    // This workspace's own undo list, rebuilt from its stored states the way a project's saved
    // history is read back: push them as already applied, step back to where it was, then put the
    // project back, since stepping back restores older states into it.
    m_swapping = true;
    {
        const QSignalBlocker blocker(&m_app->m_undoStack);
        m_app->m_undoStack.clear();
        const drift::HistoryCapture history = m_histories.take(id);
        if (!history.steps.isEmpty()) {
            const drift::Project current = project;
            for (const drift::HistoryCapture::Step &step : history.steps) {
                auto *command =
                    new drift::ProjectSnapshotCommand(&project, step.before, step.after, step.text);
                command->adoptApplied();
                m_app->m_undoStack.push(command);
            }
            m_app->m_undoStack.setIndex(history.index);
            project = current;
        }
    }
    m_swapping = false;
    emit m_app->undoHistoryChanged();
    emit m_app->undoStackChanged();

    // Every index-keyed cache (the MCP clip index among them) describes the timeline just left.
    m_app->m_mcpController->bumpEditRevision();
    m_app->notifyTracksChanged();
    restoreView();
    emit m_app->bookmarksChanged();
    emit m_app->workAreaChanged();
    emit m_app->sequenceTabsChanged();
    bump();
    return true;
}

QString WorkspaceController::addEmpty()
{
    ensureEntries();
    drift::TimelineWorkspace workspace;
    workspace.name = nextTimelineName();
    const QString id = m_app->m_project.addWorkspace(workspace);
    m_app->m_projectFile->setDirty(true);
    bump();
    switchTo(id);
    return id;
}

QString WorkspaceController::addRenderTimeline(const drift::Project &snapshot)
{
    ensureEntries();
    drift::Project &project = m_app->m_project;
    const int number = project.renderTimelineCount() + 1;
    project.setRenderTimelineCount(number);
    drift::TimelineWorkspace workspace;
    workspace.name = tr("Render timeline %1").arg(number);
    workspace.renderNumber = number;
    // A copy of its own: editing it never reaches the render's snapshot, nor the reverse.
    workspace.tracks = snapshot.rootTracks();
    workspace.bookmarks = snapshot.bookmarks();
    workspace.workAreaInUs = snapshot.workAreaInUs();
    workspace.workAreaOutUs = snapshot.workAreaOutUs();
    const QString id = project.addWorkspace(workspace);
    m_unsaved.insert(id);
    m_app->m_projectFile->setDirty(true);
    bump();
    return id;
}

QString WorkspaceController::addRenderTimelineFromOpen()
{
    return addRenderTimeline(m_app->m_project.sequenceView({}));
}

bool WorkspaceController::close(const QString &id)
{
    drift::Project &project = m_app->m_project;
    const int index = project.workspaceIndex(id);
    if (index < 0 || project.workspaces().size() <= 1)
        return false;
    if (id == project.activeWorkspaceId()) {
        const int neighbour = index > 0 ? index - 1 : index + 1;
        if (!switchTo(project.workspaces().at(neighbour).id))
            return false;
    }
    if (!project.removeWorkspace(id))
        return false;
    m_histories.remove(id);
    m_unsaved.remove(id);
    m_app->m_projectFile->setDirty(true);
    bump();
    return true;
}

bool WorkspaceController::rename(const QString &id, const QString &name)
{
    ensureEntries();
    if (!m_app->m_project.renameWorkspace(id, name))
        return false;
    m_app->m_projectFile->setDirty(true);
    bump();
    return true;
}
