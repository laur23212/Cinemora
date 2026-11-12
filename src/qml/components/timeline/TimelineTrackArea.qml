import QtQuick
import Drift

// One track's clips on the scene-graph renderer: TimelineTrackClips draws every clip in the
// viewport and owns the body gestures, and a TimelineClipItem overlay exists only for the clips
// that need trim handles, fade dots or the context menu right now. The panel's policy for where a
// moved clip lands stays here, in QML, shared by the desktop and Android panels.
Item {
    id: area

    // Owning panel (TimelinePanel or AndroidTimeline) and its track column, as TimelineClipItem
    // takes them.
    property var panel
    property var timelineColumn
    property int trackIndex: -1
    property var viewState
    property bool touchMode: false
    readonly property bool dragging: renderer.dragging

    readonly property var trackData: panel && panel.tracks && trackIndex >= 0
                                     && trackIndex < panel.tracks.length
                                     ? panel.tracks[trackIndex] : ({})

    property real lastPreviewDesired: -1
    property int lastPreviewTrack: -1

    TimelineTrackClips {
        id: renderer
        anchors.fill: parent
        viewState: area.viewState
        clipsModel: EditorState.clipsModel(area.trackIndex)
        trackIndex: area.trackIndex
        trackType: area.trackData.type || ""
        clipDisplay: area.trackData.clipDisplay !== undefined ? area.trackData.clipDisplay : 1
        showChannelWaveforms: area.trackData.showChannelWaveforms === true
        editor: EditorState
        assets: AssetLibrary
        haptics: Haptics
        // Only marks the item accessible so Qt Quick lists it; the clips it draws are exposed as
        // its children from C++ (TimelineTrackItem::installAccessibility).
        Accessible.role: Accessible.Grouping

        onMoveStarted: (clipIndex) => {
            area.lastPreviewDesired = -1
            area.lastPreviewTrack = -1
            area.panel.beginMoveFollow(area.trackIndex, clipIndex)
        }
        onMoveUpdated: (clipIndex, rect, dx, dy) => {
            area.panel.updateMoveFollow(dx, dy)
            const info = renderer.clipInfo(clipIndex)
            const desired = Math.max(0, (rect.x - Theme.clipSelectionRingWidth) / area.panel.pxPerSecond)
            const pos = renderer.mapToItem(area.timelineColumn, rect.x + rect.width / 2,
                                             rect.y + rect.height / 2)
            // Dafin: a track edge, the space past the tracks or a busy stretch of another track
            // means a new or another track; the ghost lane shows which.
            const plan = area.stackedMoveTarget(clipIndex, info, pos.y, desired)
            if (plan && plan.newTrack) {
                area.lastPreviewTrack = -2
                area.panel.clearLandingOutline()
                area.panel.pendingDropKind = info.kind || ""
                area.panel.dropNewTrackIndex = plan.insertIndex
                area.panel.dropTrackIndex = -1
                area.panel.dropStartSeconds = area.panel.snapClipStart(desired, info.duration, info.id).start
                area.panel.dropDurationSeconds = info.duration
                area.panel.dropCreatesNewTrack = true
                return
            }
            if (area.panel.dropCreatesNewTrack) {
                area.panel.dropCreatesNewTrack = false
                area.panel.pendingDropKind = ""
            }
            const targetTrack = plan ? plan.track : area.panel.trackIndexAtY(pos.y)
            const effTrack = targetTrack >= 0 ? targetTrack : area.trackIndex
            if (Math.abs(desired - area.lastPreviewDesired) * area.panel.pxPerSecond >= 1
                    || effTrack !== area.lastPreviewTrack) {
                area.lastPreviewDesired = desired
                area.lastPreviewTrack = effTrack
                area.panel.showLandingPreview(effTrack, desired, info.duration, info.id)
            }
        }
        onMoveCanceled: {
            area.panel.clearMoveFollow()
            area.panel.clearLandingPreview()
            area.panel.pendingDropKind = ""
            area.lastPreviewDesired = -1
            area.lastPreviewTrack = -1
        }
        onMoveFinished: (clipIndex, rect) => area.finishMove(clipIndex, rect)
        onContextMenuRequested: (clipIndex) => area.openContextMenu(clipIndex)
        onRenameRequested: (clipIndex) => area.panel.requestRenameClip(area.trackIndex, clipIndex)
        onToggleSelectionRequested: (clipIndex) => area.panel.toggleInSelection(area.trackIndex, clipIndex)
        onLiftedChanged: {
            if (typeof area.panel.setScrollLocked === "function")
                area.panel.setScrollLocked(renderer.lifted)
        }
    }

    // Dafin: where a moved media clip goes when that is not simply the row under it.
    // {newTrack: true, insertIndex} for a track edge or the space past the tracks; {newTrack:
    // false, track} for another track, after stacking (a busy stretch hands it on to the track it
    // stacks on); null for the usual rules (adjustments, transforms, the clip's own row).
    function stackedMoveTarget(originClip, info, columnY, startSeconds) {
        if (info && info.kind === "adjustment")
            return adjustmentEdgeTarget(info, columnY)
        if (!info || info.adjustmentKind === "transform")
            return null
        if (typeof panel.dropTargetAtYForKind !== "function" || panel.tracks[area.trackIndex].isAdjustmentLane === true)
            return null
        const target = panel.dropTargetAtYForKind(info.kind, columnY)
        if (target.newTrack)
            return target
        if (target.track === area.trackIndex || panel.tracks[target.track].isTransformLayer === true)
            return null
        return EditorState.stackTargetForClip(area.trackIndex, originClip, target.track, startSeconds)
    }

    // Dafin: an adjustment layer on its own track applies to everything below it, so where its
    // track sits is what it changes. Dragged onto a track edge (or past the last track) it moves
    // onto a new adjustment track there, as a media clip does; the middle of a row keeps Drift's
    // rules (another adjustment track, or nesting into a picture track). Lanes, transform, camera
    // and audio adjustments are left to those rules too.
    function adjustmentEdgeTarget(info, columnY) {
        const origin = panel.tracks[area.trackIndex]
        if (!origin || origin.type !== "adjustment" || origin.isAdjustmentLane === true
                || origin.isTransformLayer === true)
            return null
        if (info.adjustmentKind !== "videoEffects" && info.adjustmentKind !== "mask")
            return null
        if (typeof panel.trackOccupiesARow !== "function" || typeof panel.newTrackEdge !== "function")
            return null
        const tracks = panel.tracks
        var cursor = 0
        var firstRow = true
        for (var i = 0; i < tracks.length; i++) {
            if (!panel.trackOccupiesARow(i))
                continue
            const h = panel.trackHeight(i)
            const rowEnd = cursor + h
            if (columnY < rowEnd + Theme.trackGap / 2) {
                const edge = panel.newTrackEdge(i)
                if (firstRow && columnY < cursor + edge)
                    return { "newTrack": true, "insertIndex": i }
                if (columnY >= rowEnd - edge)
                    return { "newTrack": true, "insertIndex": i + 1 }
                return null
            }
            cursor = rowEnd + Theme.trackGap
            firstRow = false
        }
        return { "newTrack": true, "insertIndex": tracks.length }
    }

    // Same decision tree the per-clip delegate ran on release, with the indices captured at press.
    function finishMove(originClip, rect) {
        const originTrack = area.trackIndex
        panel.clearMoveFollow()
        panel.clearLandingPreview()
        panel.pendingDropKind = ""
        lastPreviewDesired = -1
        lastPreviewTrack = -1

        const info = renderer.clipInfo(originClip)
        // Snapped here, not only in the preview, so the clip lands on the outline it was showing.
        const rawStart = Math.max(0, (rect.x - Theme.clipSelectionRingWidth) / panel.pxPerSecond)
        const newStart = panel.snapClipStart(rawStart, info.duration, info.id).start
        const pos = renderer.mapToItem(timelineColumn, rect.x + rect.width / 2,
                                         rect.y + rect.height / 2)
        const target = typeof panel.dropTargetAtY === "function"
                     ? panel.dropTargetAtY(pos.y)
                     : { "track": panel.trackIndexAtY(pos.y), "lane": -1 }
        const stacked = stackedMoveTarget(originClip, info, pos.y, newStart)
        if (stacked) {
            if (stacked.newTrack)
                EditorState.moveClipToNewTrack(originTrack, originClip, stacked.insertIndex, newStart)
            else if (stacked.track !== originTrack)
                EditorState.moveClipToTrack(originTrack, originClip, stacked.track, newStart)
            else
                EditorState.moveClip(originTrack, originClip, newStart)
            return
        }
        const isAdjustment = info.kind === "adjustment"
        const wasInLane = panel.tracks[originTrack].isAdjustmentLane === true
        // Transform clips live on transform rows only, and nothing else lands on one; the C++
        // side (Track::acceptsClip) refuses the rest too, this just keeps the clip in its row.
        const isTransform = info.adjustmentKind === "transform"
        const targetIsTransform = target.track >= 0 && target.lane < 0
                                  && panel.tracks[target.track].isTransformLayer === true
        if (isTransform || targetIsTransform) {
            if (isTransform && targetIsTransform && target.track !== originTrack)
                EditorState.moveClipToTrack(originTrack, originClip, target.track, newStart)
            else
                EditorState.moveClip(originTrack, originClip, newStart)
        } else if (isAdjustment && target.lane >= 0) {
            if (target.lane !== originTrack)
                EditorState.moveClipToTrack(originTrack, originClip, target.lane, newStart)
            else
                EditorState.moveClip(originTrack, originClip, newStart)
        } else if (isAdjustment && target.track >= 0
                   && panel.tracks[target.track].type !== "adjustment") {
            EditorState.moveAdjustmentToLane(originTrack, originClip, target.track, newStart)
        } else if (isAdjustment && wasInLane && target.track < 0) {
            EditorState.moveAdjustmentToOwnTrack(originTrack, originClip, newStart)
        } else if (target.track >= 0 && target.track !== originTrack) {
            EditorState.moveClipToTrack(originTrack, originClip, target.track, newStart)
        } else {
            EditorState.moveClip(originTrack, originClip, newStart)
        }
    }

    // The clip was selected by the press that asked for this, which already gave it an overlay.
    function openContextMenu(clipIndex) {
        for (let i = 0; i < overlays.count; i++) {
            const item = overlays.itemAt(i)
            if (item && item.clipIndex === clipIndex) {
                item.openContextMenu()
                return
            }
        }
    }

    Repeater {
        id: overlays
        model: renderer.activeClips
        delegate: TimelineClipItem {
            trackClips: renderer
            panel: area.panel
            timelineColumn: area.timelineColumn
            trackIndex: area.trackIndex
            clipIndex: model.sourceIndex
            touchMode: area.touchMode
        }
    }

    // Pills and the VFR badge are drawn by the renderer; their tooltips live here, one per track.
    Loader {
        active: renderer.hoverToolTip.length > 0
        x: renderer.hoverToolTipRect.x
        y: renderer.hoverToolTipRect.y
        width: renderer.hoverToolTipRect.width
        height: renderer.hoverToolTipRect.height
        sourceComponent: ThemedToolTip {
            visible: true
            text: renderer.hoverToolTip
        }
    }

    // Touch: scroll the timeline while a picked-up clip is held near an edge.
    Timer {
        interval: 16
        repeat: true
        running: renderer.touchDragging && typeof area.panel.dragEdgeScroll === "function"

        function step(depth) { return Math.min(24, 6 + depth * 0.4) }

        onTriggered: {
            const panel = area.panel
            const px = renderer.dragPointer.x - panel.timelineViewX
            const py = renderer.mapToItem(area.timelineColumn, 0, renderer.dragPointer.y).y
                       + panel.seekHeaderHeight - panel.timelineViewY
            const edgeX = Math.min(48, panel.timelineViewW * 0.25)
            const usableTop = panel.seekHeaderHeight
            const usableH = Math.max(0, panel.timelineViewH - usableTop)
            const edgeY = Math.min(48, usableH * 0.25)
            let dx = 0
            let dy = 0
            if (px < edgeX)
                dx = -step(edgeX - px)
            else if (px > panel.timelineViewW - edgeX)
                dx = step(px - (panel.timelineViewW - edgeX))
            if (edgeY > 0) {
                if (py < usableTop + edgeY)
                    dy = -step(usableTop + edgeY - py)
                else if (py > panel.timelineViewH - edgeY)
                    dy = step(py - (panel.timelineViewH - edgeY))
            }
            if (dx === 0 && dy === 0)
                return
            panel.dragEdgeScroll(dx, dy)
            renderer.refreshDrag()
        }
    }
}
