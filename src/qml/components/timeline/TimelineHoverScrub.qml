import QtQuick
import Drift
// Dafin: hover scrub on the timeline. Rest the mouse on a video clip and a small picture floats
// by the pointer; move along the clip and it shows the frame at that position, without moving
// the playhead or playing. Passive: a HoverHandler takes no clicks, so clip drags, trims and the
// context menu work exactly as before. Pictures come from HoverScrubFrame (cached filmstrip and
// on-demand tiles), so nothing here decodes video or waits on the GUI thread.
Item {
    id: scrub

    // The TimelinePanel: track and clip lookup, zoom, row geometry.
    required property var panel
    // Visible part of the timeline content, so the picture stays on screen.
    required property real viewX
    required property real viewWidth

    // Same box as the library-drop overlay: track rows only, in content coordinates.
    x: 0
    y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
    width: parent ? parent.width : 0
    height: panel.totalTracksHeightCached

    readonly property real pointerX: hover.point.position.x
    readonly property real pointerY: hover.point.position.y
    readonly property int trackIdx: hover.hovered ? panel.trackIndexAtY(pointerY) : -1
    readonly property int clipIdx: trackIdx >= 0 ? panel.clipIndexAtPosition(trackIdx, pointerX) : -1
    readonly property var clipData: clipIdx >= 0 ? panel.tracks[trackIdx].clips[clipIdx] : null
    readonly property bool eligible: !!clipData && clipData.kind === "video"
                                     && (clipData.filmstripPath || "").length > 0
                                     && clipData.duration > 0 && !EditorState.playing
    readonly property string clipId: eligible ? clipData.id : ""

    // Shown once the pointer has rested on one clip for a moment; sweeping across the timeline
    // does not flash a picture over every clip passed.
    property bool shown: false
    onClipIdChanged: {
        shown = false
        if (clipId.length > 0)
            rest.restart()
        else
            rest.stop()
    }

    readonly property real clipFraction: {
        if (!eligible)
            return 0
        const f = Math.max(0, Math.min(1, (pointerX / panel.pxPerSecond - clipData.start) / clipData.duration))
        return clipData.reverse ? 1 - f : f
    }

    function timeLabel(seconds) {
        const s = Math.max(0, seconds)
        const m = Math.floor(s / 60)
        const rest = s - m * 60
        return m + ":" + (rest < 10 ? "0" : "") + rest.toFixed(1)
    }

    HoverHandler {
        id: hover
    }

    Timer {
        id: rest
        interval: 220
        onTriggered: scrub.shown = scrub.clipId.length > 0
    }

    Rectangle {
        id: card
        readonly property real cardW: 176
        readonly property real cardH: 99
        readonly property real rowTop: scrub.trackIdx >= 0 ? scrub.panel.trackOffsetY(scrub.trackIdx) : 0
        readonly property real rowH: scrub.trackIdx >= 0 ? scrub.panel.trackHeight(scrub.trackIdx) : 0

        visible: scrub.shown && scrub.eligible && frame.visible
        width: cardW
        height: cardH
        radius: 4
        color: "black"
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.35)
        z: 260
        x: Math.max(scrub.viewX + 4,
                    Math.min(scrub.viewX + scrub.viewWidth - width - 4, scrub.pointerX - width / 2))
        // Above the row; below it when there is no room under the ruler.
        y: rowTop - height - 6 >= -Theme.timelineRulerHeight ? rowTop - height - 6 : rowTop + rowH + 6

        HoverScrubFrame {
            id: frame
            anchors.fill: parent
            anchors.margins: 1
            active: scrub.shown && scrub.eligible
            fraction: scrub.clipFraction
            showLine: false
            filmstripPath: scrub.eligible ? scrub.clipData.filmstripPath : ""
            sourcePath: scrub.eligible ? scrub.clipData.path : ""
            inPoint: scrub.eligible ? scrub.clipData.inPoint : 0
            outPoint: scrub.eligible ? scrub.clipData.outPoint : 0
            sourceDuration: scrub.eligible ? scrub.clipData.sourceDuration : 0
            rotationCorrection: scrub.eligible ? scrub.clipData.rotationCorrection : 0
        }

        Text {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 4
            text: scrub.timeLabel(frame.sourceSeconds)
            color: "white"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            style: Text.Outline
            styleColor: "black"
        }
    }
}
