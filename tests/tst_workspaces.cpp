// Dafin: workspaces, the project's separate main timelines (core/project/ProjectWorkspaces.cpp).
#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>

#include "core/edit/commands/ProjectCommands.h"
#include "core/project/Project.h"
#include "core/project/clip/Clip.h"

using drift::Clip;
using drift::Project;
using drift::TimelineWorkspace;
using drift::Track;
using drift::TrackType;

namespace {

QString addMedia(Project &project, const QString &name)
{
    drift::MediaAsset asset;
    asset.name = name;
    asset.path = QStringLiteral("/media/%1.mp4").arg(name);
    asset.kind = drift::MediaKind::Video;
    return project.addAsset(asset);
}

Clip clipFor(const QString &assetId, const QString &id)
{
    Clip clip;
    clip.id = id;
    clip.assetId = assetId;
    return clip;
}

int clipCount(const QList<Track> &tracks)
{
    int count = 0;
    for (const Track &track : tracks)
        count += track.clips.size();
    return count;
}

} // namespace

class WorkspacesTest : public QObject
{
    Q_OBJECT

private slots:
    void switchingSwapsOnlyTheTimeline();
    void otherWorkspacesAreReachedByTrackPasses();
    void savedAndReopened();
    void undoNeverReachesAnotherWorkspace();
    void activeWorkspaceCannotBeRemoved();
};

void WorkspacesTest::switchingSwapsOnlyTheTimeline()
{
    Project project;
    const QString media = addMedia(project, QStringLiteral("a"));
    project.tracks()[0].clips.append(clipFor(media, QStringLiteral("c1")));
    project.bookmarks().append({1000000, QStringLiteral("mark")});
    project.ensureWorkspaces(QStringLiteral("Main timeline"));
    const QString mainId = project.activeWorkspaceId();

    TimelineWorkspace copy;
    copy.name = QStringLiteral("Render timeline 1");
    copy.renderNumber = 1;
    copy.tracks = project.tracks();
    const QString renderId = project.addWorkspace(copy);
    const QString emptyId = project.addWorkspace(TimelineWorkspace{.name = QStringLiteral("Timeline 2")});
    QCOMPARE(project.workspaces().size(), 3);

    QVERIFY(project.activateWorkspace(emptyId));
    QCOMPARE(project.activeWorkspaceId(), emptyId);
    QCOMPARE(clipCount(project.tracks()), 0);
    QVERIFY(project.bookmarks().isEmpty());
    // Media stays shared.
    QVERIFY(project.asset(media));

    // An edit in the render copy leaves the main timeline alone.
    QVERIFY(project.activateWorkspace(renderId));
    QCOMPARE(clipCount(project.tracks()), 1);
    project.tracks()[0].clips.append(clipFor(media, QStringLiteral("c2")));
    QVERIFY(project.activateWorkspace(mainId));
    QCOMPARE(clipCount(project.tracks()), 1);
    QCOMPARE(project.bookmarks().size(), 1);
    QVERIFY(project.activateWorkspace(renderId));
    QCOMPARE(clipCount(project.tracks()), 2);
}

void WorkspacesTest::otherWorkspacesAreReachedByTrackPasses()
{
    Project project;
    const QString media = addMedia(project, QStringLiteral("a"));
    project.ensureWorkspaces(QStringLiteral("Main timeline"));
    TimelineWorkspace other;
    other.tracks = {{.type = TrackType::Video}};
    other.tracks[0].clips.append(clipFor(media, QStringLiteral("c1")));
    project.addWorkspace(other);

    // Media in use, save embedding and path fixes all go through forEachTrackList.
    int seen = 0;
    project.forEachTrackList([&](const QList<Track> &tracks) { seen += clipCount(tracks); });
    QCOMPARE(seen, 1);
}

void WorkspacesTest::savedAndReopened()
{
    Project project;
    const QString media = addMedia(project, QStringLiteral("a"));
    project.ensureWorkspaces(QStringLiteral("Main timeline"));
    TimelineWorkspace other;
    other.name = QStringLiteral("Render timeline 1");
    other.renderNumber = 1;
    other.tracks = {{.type = TrackType::Video}};
    other.tracks[0].clips.append(clipFor(media, QStringLiteral("c1")));
    other.playheadUs = 2500000;
    other.zoom = 3.0;
    other.selectedClipIds = {QStringLiteral("c1")};
    const QString otherId = project.addWorkspace(other);
    project.setRenderTimelineCount(1);

    // Undo states (compact JSON) leave workspaces out; the file keeps them.
    QVERIFY(!project.toJson(false).contains(QStringLiteral("workspaces")));
    QString error;
    Project reopened = Project::fromJson(project.toJson(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(reopened.workspaces().size(), 2);
    QCOMPARE(reopened.activeWorkspaceId(), project.activeWorkspaceId());
    QCOMPARE(reopened.renderTimelineCount(), 1);
    const TimelineWorkspace &back = reopened.workspaces().at(reopened.workspaceIndex(otherId));
    QCOMPARE(back.name, QStringLiteral("Render timeline 1"));
    QCOMPARE(back.renderNumber, 1);
    QCOMPARE(back.playheadUs, drift::TimeUs(2500000));
    QCOMPARE(back.zoom, 3.0);
    QCOMPARE(back.selectedClipIds, QStringList{QStringLiteral("c1")});
    QCOMPARE(clipCount(back.tracks), 1);
    QVERIFY(reopened.activateWorkspace(otherId));
    QCOMPARE(clipCount(reopened.tracks()), 1);
}

void WorkspacesTest::undoNeverReachesAnotherWorkspace()
{
    Project project;
    project.ensureWorkspaces(QStringLiteral("Main timeline"));
    const QString mainId = project.activeWorkspaceId();
    const QString otherId = project.addWorkspace(TimelineWorkspace{.name = QStringLiteral("Timeline 2")});

    // An edit on the main timeline, recorded for undo.
    const Project before = project;
    const QString first = addMedia(project, QStringLiteral("a"));
    project.tracks()[0].clips.append(clipFor(first, QStringLiteral("c1")));
    QUndoStack stack;
    auto *command = new drift::ProjectSnapshotCommand(&project, before, project, QStringLiteral("Add clip"));
    command->adoptApplied();
    stack.push(command);

    // Meanwhile the other workspace gets new media and a clip of it.
    QVERIFY(project.activateWorkspace(otherId));
    const QString second = addMedia(project, QStringLiteral("b"));
    project.tracks()[0].clips.append(clipFor(second, QStringLiteral("c2")));
    QVERIFY(project.activateWorkspace(mainId));

    stack.undo();
    QCOMPARE(clipCount(project.tracks()), 0);
    QCOMPARE(project.workspaces().size(), 2);
    QCOMPARE(project.activeWorkspaceId(), mainId);
    // The older state had no "b", but the other workspace still plays it.
    QVERIFY(project.asset(second));
    QVERIFY(!project.asset(first));
    QVERIFY(project.activateWorkspace(otherId));
    QCOMPARE(clipCount(project.tracks()), 1);
}

void WorkspacesTest::activeWorkspaceCannotBeRemoved()
{
    Project project;
    project.ensureWorkspaces(QStringLiteral("Main timeline"));
    const QString mainId = project.activeWorkspaceId();
    const QString otherId = project.addWorkspace(TimelineWorkspace{.name = QStringLiteral("Timeline 2")});
    QVERIFY(!project.removeWorkspace(mainId));
    QVERIFY(project.removeWorkspace(otherId));
    QCOMPARE(project.workspaces().size(), 1);
    QVERIFY(!project.activateWorkspace(otherId));
}

QTEST_GUILESS_MAIN(WorkspacesTest)
#include "tst_workspaces.moc"
