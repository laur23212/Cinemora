// Dafin: workspaces, the project's separate main timelines (see TimelineWorkspace in Project.h).

#include "core/project/Project.h"
#include "core/project/clip/Clip.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QUuid>

namespace drift {

int Project::workspaceIndex(const QString &id) const
{
    for (int i = 0; i < m_workspaces.size(); ++i) {
        if (m_workspaces.at(i).id == id)
            return i;
    }
    return -1;
}

void Project::ensureWorkspaces(const QString &name)
{
    if (!m_workspaces.isEmpty() && workspaceIndex(m_activeWorkspaceId) >= 0)
        return;
    TimelineWorkspace open;
    open.id = m_activeWorkspaceId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces)
                                            : m_activeWorkspaceId;
    open.name = name;
    m_workspaces.prepend(open);
    m_activeWorkspaceId = open.id;
}

QString Project::addWorkspace(TimelineWorkspace workspace)
{
    if (workspace.id.isEmpty() || workspaceIndex(workspace.id) >= 0)
        workspace.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (workspace.tracks.isEmpty())
        workspace.tracks = {{.type = TrackType::Video}};
    drift::ensureTrackIds(workspace.tracks);
    m_workspaces.append(workspace);
    return workspace.id;
}

bool Project::removeWorkspace(const QString &id)
{
    const int index = workspaceIndex(id);
    if (index < 0 || id == m_activeWorkspaceId)
        return false;
    m_workspaces.removeAt(index);
    return true;
}

bool Project::renameWorkspace(const QString &id, const QString &name)
{
    const int index = workspaceIndex(id);
    const QString trimmed = name.trimmed();
    if (index < 0 || trimmed.isEmpty())
        return false;
    m_workspaces[index].name = trimmed;
    return true;
}

TimelineWorkspace *Project::activeWorkspace()
{
    const int index = workspaceIndex(m_activeWorkspaceId);
    return index < 0 ? nullptr : &m_workspaces[index];
}

bool Project::activateWorkspace(const QString &id)
{
    if (!m_activeSequenceId.isEmpty())
        return false;
    const int to = workspaceIndex(id);
    if (to < 0)
        return false;
    if (id == m_activeWorkspaceId)
        return true;

    if (TimelineWorkspace *from = activeWorkspace()) {
        from->tracks = m_tracks;
        from->bookmarks = m_bookmarks;
        from->workAreaInUs = m_workAreaInUs;
        from->workAreaOutUs = m_workAreaOutUs;
    }

    TimelineWorkspace &target = m_workspaces[to];
    m_tracks = target.tracks;
    if (m_tracks.isEmpty())
        m_tracks = {{.type = TrackType::Video}};
    ensureTrackIds();
    m_bookmarks = target.bookmarks;
    m_workAreaInUs = target.workAreaInUs;
    m_workAreaOutUs = target.workAreaOutUs;
    if (!hasWorkArea())
        clearWorkArea();
    // Stale while active; dropping them keeps only one copy of the timeline in memory.
    target.tracks = TrackList();
    target.bookmarks.clear();
    m_activeWorkspaceId = id;
    return true;
}

void Project::keepWorkspacesFrom(const Project &live)
{
    m_workspaces = live.m_workspaces;
    m_activeWorkspaceId = live.m_activeWorkspaceId;
    m_renderTimelineCount = live.m_renderTimelineCount;
    if (m_workspaces.size() < 2)
        return;

    // What the other workspaces play: media by asset id, composites by sequence id. A composite
    // brought back can itself play media and composites, hence the work list.
    // Plain QLists: they share the tracks rather than deep-copying them as a TrackList would.
    QList<QList<Track>> pending;
    for (const TimelineWorkspace &workspace : std::as_const(m_workspaces)) {
        if (workspace.id != m_activeWorkspaceId)
            pending.append(static_cast<const QList<Track> &>(workspace.tracks));
    }
    QSet<QString> seenSequences;
    while (!pending.isEmpty()) {
        const QList<Track> tracks = pending.takeLast();
        for (const Track &track : tracks) {
            for (const Clip &clip : track.clips) {
                if (!clip.assetId.isEmpty() && !m_assetsById.contains(clip.assetId)) {
                    if (const MediaAsset *asset = live.asset(clip.assetId)) {
                        m_assetsById.insert(clip.assetId, *asset);
                        if (!m_assetOrder.contains(clip.assetId))
                            m_assetOrder.append(clip.assetId);
                    }
                }
                const QString sequenceId = clip.sequenceId;
                if (sequenceId.isEmpty() || seenSequences.contains(sequenceId))
                    continue;
                seenSequences.insert(sequenceId);
                if (!m_sequences.contains(sequenceId) && sequenceId != m_activeSequenceId
                    && live.hasSequence(sequenceId)) {
                    Sequence sequence;
                    sequence.id = sequenceId;
                    sequence.tracks = live.sequenceTracks(sequenceId);
                    m_sequences.insert(sequenceId, sequence);
                    pending.append(live.sequenceTracks(sequenceId));
                }
            }
        }
    }
}

void workspacesFromJson(Project &project, const QJsonObject &object,
                        const std::function<TrackList(const QJsonArray &)> &tracksFromJson)
{
    project.m_workspaces.clear();
    project.m_activeWorkspaceId.clear();
    project.m_renderTimelineCount =
        qMax(0, object.value(QStringLiteral("renderTimelineCount")).toInt(0));
    const QJsonArray array = object.value(QStringLiteral("workspaces")).toArray();
    if (array.isEmpty())
        return;
    const QString activeId = object.value(QStringLiteral("activeWorkspaceId")).toString();
    for (const QJsonValue &value : array) {
        const QJsonObject entry = value.toObject();
        TimelineWorkspace workspace;
        workspace.id = entry.value(QStringLiteral("id")).toString();
        if (workspace.id.isEmpty() || project.workspaceIndex(workspace.id) >= 0)
            continue;
        workspace.name = entry.value(QStringLiteral("name")).toString();
        workspace.renderNumber = qMax(0, entry.value(QStringLiteral("renderNumber")).toInt(0));
        if (workspace.id != activeId) {
            workspace.tracks = tracksFromJson(entry.value(QStringLiteral("tracks")).toArray());
            if (workspace.tracks.isEmpty())
                workspace.tracks = {{.type = TrackType::Video}};
            drift::ensureTrackIds(workspace.tracks);
            for (const QJsonValue &mark : entry.value(QStringLiteral("bookmarks")).toArray()) {
                const QJsonObject markObject = mark.toObject();
                Bookmark bookmark;
                bookmark.timeUs =
                    static_cast<TimeUs>(markObject.value(QStringLiteral("timeUs")).toDouble());
                bookmark.label = markObject.value(QStringLiteral("label")).toString();
                workspace.bookmarks.append(bookmark);
            }
            workspace.workAreaInUs =
                static_cast<TimeUs>(entry.value(QStringLiteral("workAreaInUs")).toDouble(-1));
            workspace.workAreaOutUs =
                static_cast<TimeUs>(entry.value(QStringLiteral("workAreaOutUs")).toDouble(-1));
            workspace.playheadUs =
                qMax<TimeUs>(0, static_cast<TimeUs>(entry.value(QStringLiteral("playheadUs")).toDouble(0)));
            workspace.zoom = entry.value(QStringLiteral("zoom")).toDouble(0.0);
            workspace.scrollX = entry.value(QStringLiteral("scrollX")).toDouble(0.0);
            for (const QJsonValue &clipId : entry.value(QStringLiteral("selection")).toArray())
                workspace.selectedClipIds.append(clipId.toString());
        }
        project.m_workspaces.append(workspace);
    }
    if (project.m_workspaces.isEmpty())
        return;
    // The file's "tracks" are the active workspace's. A file whose active entry went missing
    // gets one back for them, rather than handing them to another workspace.
    if (project.workspaceIndex(activeId) < 0) {
        TimelineWorkspace open;
        open.id = activeId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : activeId;
        open.name = QStringLiteral("Main timeline");
        project.m_workspaces.prepend(open);
        project.m_activeWorkspaceId = open.id;
    } else {
        project.m_activeWorkspaceId = activeId;
    }
}

void workspacesToJson(const Project &project, QJsonObject &root,
                      const std::function<QJsonArray(const QList<Track> &)> &tracksToJson)
{
    if (project.m_renderTimelineCount > 0)
        root.insert(QStringLiteral("renderTimelineCount"), project.m_renderTimelineCount);
    if (project.m_workspaces.isEmpty())
        return;
    QJsonArray array;
    for (const TimelineWorkspace &workspace : project.m_workspaces) {
        QJsonObject entry{
            {QStringLiteral("id"), workspace.id},
            {QStringLiteral("name"), workspace.name},
        };
        if (workspace.renderNumber > 0)
            entry.insert(QStringLiteral("renderNumber"), workspace.renderNumber);
        if (workspace.id != project.m_activeWorkspaceId) {
            entry.insert(QStringLiteral("tracks"), tracksToJson(workspace.tracks));
            QJsonArray bookmarks;
            for (const Bookmark &bookmark : workspace.bookmarks) {
                bookmarks.append(QJsonObject{
                    {QStringLiteral("timeUs"), static_cast<double>(bookmark.timeUs)},
                    {QStringLiteral("label"), bookmark.label},
                });
            }
            if (!bookmarks.isEmpty())
                entry.insert(QStringLiteral("bookmarks"), bookmarks);
            if (workspace.workAreaInUs >= 0 && workspace.workAreaOutUs > workspace.workAreaInUs) {
                entry.insert(QStringLiteral("workAreaInUs"), static_cast<double>(workspace.workAreaInUs));
                entry.insert(QStringLiteral("workAreaOutUs"), static_cast<double>(workspace.workAreaOutUs));
            }
            entry.insert(QStringLiteral("playheadUs"), static_cast<double>(workspace.playheadUs));
            if (workspace.zoom > 0.0) {
                entry.insert(QStringLiteral("zoom"), workspace.zoom);
                entry.insert(QStringLiteral("scrollX"), workspace.scrollX);
            }
            if (!workspace.selectedClipIds.isEmpty())
                entry.insert(QStringLiteral("selection"),
                             QJsonArray::fromStringList(workspace.selectedClipIds));
        }
        array.append(entry);
    }
    root.insert(QStringLiteral("workspaces"), array);
    root.insert(QStringLiteral("activeWorkspaceId"), project.m_activeWorkspaceId);
}

} // namespace drift
