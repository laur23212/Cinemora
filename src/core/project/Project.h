#pragma once

#include "core/project/BinFolder.h"
#include "core/project/media/MediaAsset.h"
#include "core/project/Transcript.h"
#include "core/project/Track.h"
#include "core/base/Time.h"

#include <QColor>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <initializer_list>

namespace drift {

struct Bookmark
{
    TimeUs timeUs = 0;
    QString label;
    // Dafin: marker colour as "#rrggbb"; empty draws the theme's marker colour.
    QString color;
};

// How the canvas area behind/around clips is filled.
enum class BackgroundKind { Color, Blur, Transparent };

struct Background
{
    BackgroundKind kind = BackgroundKind::Color;
    QColor color = Qt::black;    // used when kind == Color
    double blurStrength = 20.0;  // px blur radius; used when kind == Blur
};

inline QString backgroundKindToString(BackgroundKind kind)
{
    switch (kind) {
    case BackgroundKind::Blur:
        return QStringLiteral("blur");
    case BackgroundKind::Transparent:
        return QStringLiteral("transparent");
    case BackgroundKind::Color:
    default:
        return QStringLiteral("color");
    }
}

inline BackgroundKind backgroundKindFromString(const QString &kind)
{
    if (kind == QLatin1String("blur"))
        return BackgroundKind::Blur;
    if (kind == QLatin1String("transparent"))
        return BackgroundKind::Transparent;
    return BackgroundKind::Color;
}

// A track list that never shares its buffer with the list it was copied from.
//
// Project has value semantics on purpose: the undo stack and every edit path snapshot it by
// copy and then keep mutating the original — very often through a `Track &` or `Clip &` that
// was bound *before* the copy was taken. QList is copy-on-write, so a plain memberwise copy
// leaves both lists pointing at one buffer, and a write through such a reference bypasses
// QList's detach and lands in the snapshot as well as in the project. That silently broke undo
// for most clip edits: the "before" state was mutated into the "after" state, so undoing
// restored the value that had just been set.
//
// Detaching here, on the copy, is what fixes it: the copy gets its own buffers and the original
// keeps sole ownership of its, so references into the original stay valid *and* private. Two
// levels are needed and sufficient — the clip buffers are what those stale references point
// into, and everything below a Clip is reached through a detaching accessor at write time.
struct TrackList : QList<Track>
{
    using QList<Track>::QList;

    TrackList() = default;
    TrackList(const TrackList &other) : QList<Track>(other) { deepDetach(); }
    TrackList(const QList<Track> &other) : QList<Track>(other) { deepDetach(); }
    TrackList(TrackList &&) = default;

    TrackList &operator=(const TrackList &other)
    {
        QList<Track>::operator=(other);
        deepDetach();
        return *this;
    }
    TrackList &operator=(const QList<Track> &other)
    {
        QList<Track>::operator=(other);
        deepDetach();
        return *this;
    }
    TrackList &operator=(TrackList &&) = default;
    // Disambiguates `m_tracks = {...}`, which would otherwise match both the TrackList and the
    // QList overloads above. A fresh list shares nothing, so there is nothing to detach.
    TrackList &operator=(std::initializer_list<Track> items)
    {
        QList<Track>::operator=(items);
        return *this;
    }

    void deepDetach()
    {
        detach();
        for (Track &track : *this) {
            track.clips.detach();
            track.transitions.detach();
        }
    }
};

// Mints missing/duplicate track ids and repairs orphaned lanes; see Project::ensureTrackIds.
void ensureTrackIds(QList<Track> &tracks);

// A nested timeline played by Composite clips. Canvas size, fps and sample rate are the project's.
struct Sequence
{
    QString id;
    TrackList tracks;
};

// Dafin: a workspace, one of the project's separate main timelines (WorkspaceController). Media,
// composites and project settings are shared; each workspace has its own tracks, bookmarks, work
// area and view. The active workspace's values live in the Project's own fields, so its entry
// holds only its id and name (the rest is stale), as with an active Sequence.
struct TimelineWorkspace
{
    QString id;
    QString name;
    // > 0 for a "Render timeline N" made when an export was queued.
    int renderNumber = 0;
    TrackList tracks;
    QList<Bookmark> bookmarks;
    TimeUs workAreaInUs = -1;
    TimeUs workAreaOutUs = -1;
    TimeUs playheadUs = 0;
    // Timeline zoom and scroll as the timeline panel last showed them; zoom 0 = not known.
    double zoom = 0.0;
    double scrollX = 0.0;
    QStringList selectedClipIds;
};

// Root project document: tracks, assets, output settings.
class Project
{
public:
    static constexpr int kCurrentVersion = 23;

    Project() { resetToDefaultTimeline(); }

    QString name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }

    // Stable across saves; names the extraction directory a packaged project unpacks into, so it
    // must survive a round-trip rather than being reminted per save.
    QString id() const { return m_id; }
    void setId(const QString &id) { m_id = id; }

    QString author() const { return m_author; }
    void setAuthor(const QString &author) { m_author = author; }

    QString description() const { return m_description; }
    void setDescription(const QString &description) { m_description = description; }

    QDateTime createdAt() const { return m_createdAt; }
    void setCreatedAt(const QDateTime &createdAt) { m_createdAt = createdAt; }

    QDateTime modifiedAt() const { return m_modifiedAt; }
    void setModifiedAt(const QDateTime &modifiedAt) { m_modifiedAt = modifiedAt; }

    int fps() const { return m_fps; }
    void setFps(int fps) { m_fps = qMax(1, fps); }

    int width() const { return m_width; }
    int height() const { return m_height; }
    int sampleRate() const { return m_sampleRate; }
    void setResolution(int width, int height) { m_width = width; m_height = height; }
    void setSampleRate(int rate) { m_sampleRate = rate; }

    // The tracks of the timeline open for editing: the main timeline, or the composite sequence
    // named by activeSequenceId(). Every edit path works on these, so opening a composite swaps
    // its tracks in here rather than teaching each of them about nesting.
    const QList<Track> &tracks() const { return m_tracks; }
    QList<Track> &tracks() { return m_tracks; }

    // Empty = the main timeline.
    QString activeSequenceId() const { return m_activeSequenceId; }
    // Stores the open tracks back into their slot and loads `id`'s. False for an unknown id.
    bool activateSequence(const QString &id);
    bool hasSequence(const QString &id) const { return id.isEmpty() || m_sequences.contains(id); }
    // Tracks of any timeline, wherever they currently live. Empty id = the main timeline.
    const QList<Track> &sequenceTracks(const QString &id) const;
    const QList<Track> &rootTracks() const { return sequenceTracks({}); }
    TimeUs sequenceDurationUs(const QString &id) const;
    QStringList sequenceIds() const { return m_sequences.keys(); }
    // A copy with `id` active, for rendering that timeline through code that reads tracks().
    Project sequenceView(const QString &id) const;
    QString addSequence(TrackList tracks);
    // Must not be the active sequence.
    void removeSequence(const QString &id);

    // Every timeline's tracks — the open one, the main one and each composite's — for passes that
    // must reach clips wherever they live (path remaps, packaging).
    template <typename Fn>
    void forEachTrackList(Fn &&fn)
    {
        fn(static_cast<QList<Track> &>(m_tracks));
        if (!m_activeSequenceId.isEmpty())
            fn(static_cast<QList<Track> &>(m_rootTracks));
        for (auto it = m_sequences.begin(); it != m_sequences.end(); ++it) {
            if (it.key() != m_activeSequenceId)
                fn(static_cast<QList<Track> &>(it->tracks));
        }
        // Dafin: the other workspaces' timelines.
        for (TimelineWorkspace &workspace : m_workspaces) {
            if (workspace.id != m_activeWorkspaceId)
                fn(static_cast<QList<Track> &>(workspace.tracks));
        }
    }
    template <typename Fn>
    void forEachTrackList(Fn &&fn) const
    {
        fn(tracks());
        if (!m_activeSequenceId.isEmpty())
            fn(rootTracks());
        for (const QString &id : m_sequences.keys()) {
            if (id != m_activeSequenceId)
                fn(sequenceTracks(id));
        }
        for (const TimelineWorkspace &workspace : m_workspaces) {
            if (workspace.id != m_activeWorkspaceId)
                fn(static_cast<const QList<Track> &>(workspace.tracks));
        }
    }

    // Dafin workspaces (ProjectWorkspaces.cpp). Empty list = the one implicit workspace of a
    // project that never had a second one. Not undoable: ProjectSnapshotCommand keeps the live
    // set across undo/redo (keepWorkspacesFrom), and the undo history's states leave it out.
    const QList<TimelineWorkspace> &workspaces() const { return m_workspaces; }
    QString activeWorkspaceId() const { return m_activeWorkspaceId; }
    int workspaceIndex(const QString &id) const;
    // Gives the open timeline an entry, named `name`, when there is none yet.
    void ensureWorkspaces(const QString &name);
    // Mints an id when it is empty; returns it. Needs ensureWorkspaces first.
    QString addWorkspace(TimelineWorkspace workspace);
    // Not the active one. False when refused.
    bool removeWorkspace(const QString &id);
    bool renameWorkspace(const QString &id, const QString &name);
    // Stores the open main timeline into the active entry and loads `id`'s. Refused (false) while
    // a composite is open or for an unknown id. View fields (playhead…) are the caller's to fill.
    bool activateWorkspace(const QString &id);
    // The active entry, to record its view before switching away; null when there is none.
    TimelineWorkspace *activeWorkspace();
    // How many render timelines have been made, for the next one's number.
    int renderTimelineCount() const { return m_renderTimelineCount; }
    void setRenderTimelineCount(int count) { m_renderTimelineCount = qMax(0, count); }
    // After an undo/redo put an older state in: the live workspaces come back, and so does any
    // media or composite another workspace still uses that the older state had not got yet.
    void keepWorkspacesFrom(const Project &live);

    // Composite tabs the user has open, in tab order. Saved with the project.
    const QStringList &openSequenceTabs() const { return m_openSequenceTabs; }
    QStringList &openSequenceTabs() { return m_openSequenceTabs; }

    const QList<QString> &assetOrder() const { return m_assetOrder; }
    QList<QString> &assetOrder() { return m_assetOrder; }
    const QHash<QString, MediaAsset> &assets() const { return m_assetsById; }
    QHash<QString, MediaAsset> &assets() { return m_assetsById; }

    const QList<QString> &binFolderOrder() const { return m_binFolderOrder; }
    QList<QString> &binFolderOrder() { return m_binFolderOrder; }
    const QHash<QString, BinFolder> &binFolders() const { return m_binFoldersById; }
    QHash<QString, BinFolder> &binFolders() { return m_binFoldersById; }

    const QList<Bookmark> &bookmarks() const { return m_bookmarks; }
    QList<Bookmark> &bookmarks() { return m_bookmarks; }

    // Timeline work area (Mark In / Mark Out). Unset markers use -1.
    TimeUs workAreaInUs() const { return m_workAreaInUs; }
    TimeUs workAreaOutUs() const { return m_workAreaOutUs; }
    void setWorkAreaInUs(TimeUs us) { m_workAreaInUs = us; }
    void setWorkAreaOutUs(TimeUs us) { m_workAreaOutUs = us; }
    void clearWorkArea()
    {
        m_workAreaInUs = -1;
        m_workAreaOutUs = -1;
    }
    bool hasWorkArea() const { return m_workAreaInUs >= 0 && m_workAreaOutUs > m_workAreaInUs; }

    const Background &background() const { return m_background; }
    void setBackground(const Background &background) { m_background = background; }

    void resetToDefaultTimeline();
    TimeUs durationUs() const;

    // Mints an id for every track that lacks one, and drops a nested adjustment lane's
    // `parentTrackId` when the track it names is gone. Track creation is spread over a dozen
    // call sites (plus tests, which build bare `Track{.type = …}` aggregates), so minting is
    // centralised here rather than duplicated: anything that appends a track can leave the id
    // empty and this makes it valid. Idempotent — existing ids are never rewritten.
    void ensureTrackIds();

    int trackIndexById(const QString &id) const;

    // Copy that uniquely owns its Qt containers. A plain `Project copy = *this` shares
    // QMap/QList payloads via implicit sharing; mutating either side while another thread
    // reads the other is a use-after-free. Call this before handing a snapshot to a worker.
    Project detachedCopy() const;

    QString addAsset(MediaAsset asset);
    MediaAsset *asset(const QString &id);
    const MediaAsset *asset(const QString &id) const;
    int assetIndex(const QString &id) const;
    QString assetIdAt(int index) const;

    // Word-level transcripts keyed by asset id. Not undoable: a transcript can cost money to make,
    // so ProjectSnapshotCommand carries the live set across undo/redo, and entries outlive their
    // asset so undoing a removal brings the transcript back with it.
    TranscriptPtr transcript(const QString &assetId) const { return m_transcripts.value(assetId); }
    void setTranscript(const QString &assetId, TranscriptPtr transcript);
    const QHash<QString, TranscriptPtr> &transcripts() const { return m_transcripts; }
    void setTranscripts(const QHash<QString, TranscriptPtr> &transcripts) { m_transcripts = transcripts; }

    QString addBinFolder(BinFolder folder);
    BinFolder *binFolder(const QString &id);
    const BinFolder *binFolder(const QString &id) const;
    int binFolderIndex(const QString &id) const;
    QString binFolderIdAt(int index) const;

    static Project fromJson(const QJsonObject &object, QString *errorOut = nullptr);
    QJsonObject toJson(bool includeTranscripts = true) const;
    // Compact JSON of toJson(false); SHA-256 hex of those bytes. Undo history and on-disk
    // history snapshots share this so a file named <hash>.json hashes back to <hash>.
    // Transcripts are left out: they are not undoable state, and hashing hundreds of KB of
    // words on every edit would be wasted work.
    QByteArray toCompactJson() const;
    QString contentHash() const;

private:
    QString m_name = QStringLiteral("Untitled Project");
    QString m_id;
    QString m_author;
    QString m_description;
    QDateTime m_createdAt;
    QDateTime m_modifiedAt;
    int m_fps = 30;
    int m_width = 1920;
    int m_height = 1080;
    int m_sampleRate = 48000;
    TrackList m_tracks;
    // Main timeline's tracks while a composite is active; stale otherwise (m_tracks holds them).
    TrackList m_rootTracks;
    // A sequence's entry is stale while it is the active one.
    QHash<QString, Sequence> m_sequences;
    QString m_activeSequenceId;
    QStringList m_openSequenceTabs;
    QList<Bookmark> m_bookmarks;
    TimeUs m_workAreaInUs = -1;
    TimeUs m_workAreaOutUs = -1;
    Background m_background;
    QList<QString> m_assetOrder;
    QHash<QString, MediaAsset> m_assetsById;
    QList<QString> m_binFolderOrder;
    QHash<QString, BinFolder> m_binFoldersById;
    QHash<QString, TranscriptPtr> m_transcripts;
    // Dafin workspaces.
    QList<TimelineWorkspace> m_workspaces;
    QString m_activeWorkspaceId;
    int m_renderTimelineCount = 0;

    // ProjectWorkspaces.cpp: reads/writes the "workspaces" block with Project.cpp's track codec.
    friend void workspacesFromJson(Project &project, const QJsonObject &object,
                                   const std::function<TrackList(const QJsonArray &)> &tracksFromJson);
    friend void workspacesToJson(const Project &project, QJsonObject &root,
                                 const std::function<QJsonArray(const QList<Track> &)> &tracksToJson);
};

void workspacesFromJson(Project &project, const QJsonObject &object,
                        const std::function<TrackList(const QJsonArray &)> &tracksFromJson);
void workspacesToJson(const Project &project, QJsonObject &root,
                      const std::function<QJsonArray(const QList<Track> &)> &tracksToJson);

} // namespace drift
