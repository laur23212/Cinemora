import QtQuick
import Drift

// Playhead line and scrubber head.
Item {
    id: playhead

    property var panel
    property Flickable view
    property var interpolator
    readonly property bool headDragging: playheadDragArea.drag.active
    // The scissors chip on the head: a press-and-release on it without moving cuts at the bar.
    readonly property real scissorsHitHeight: 22
    y: 0
    // Above the seek strip (z: 2), otherwise the scrub area
    // covers the handle and swallows the press before drag starts.
    z: 3
    width: Theme.playheadLineWidth
    height: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
            + panel.totalTracksHeightCached

    Binding {
        target: playhead
        property: "x"
        value: interpolator.seconds * panel.pxPerSecond
        when: !playheadDragArea.drag.active && !playheadLineDrag.drag.active
    }

    // Follow either drag live so the preview scrubs with it.
    onXChanged: {
        if (playheadDragArea.drag.active || playheadLineDrag.drag.active)
            EditorState.playheadSeconds = playhead.x / panel.pxPerSecond
    }

    function finishSeek() {
        EditorState.playheadSeconds =
            EditorState.snapTime(playhead.x / panel.pxPerSecond)
    }

    TimelinePlayheadLine { }

    TimelinePlayheadHead { }

    // Wide grab across the seek strip head.
    MouseArea {
        id: playheadDragArea
        width: Theme.playheadSeekGrabWidth
        height: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
        x: -(width - Theme.playheadLineWidth) / 2
        y: 0
        cursorShape: Qt.SizeHorCursor
        preventStealing: true
        hoverEnabled: true
        drag.target: playhead
        drag.axis: Drag.XAxis
        drag.threshold: 0
        drag.minimumX: 0
        drag.maximumX: view.contentWidth - Theme.playheadLineWidth
        property real pressStartX: 0
        property bool pressedOnScissors: false
        onPressed: (mouse) => {
            pressStartX = playhead.x
            pressedOnScissors = mouse.y < playhead.scissorsHitHeight
            panel.beginPlayheadSeek()
        }
        onReleased: {
            playhead.finishSeek()
            // Scissors: nothing selected cuts every clip under the bar, a selection cuts only that.
            if (pressedOnScissors && Math.abs(playhead.x - pressStartX) < 2)
                EditorState.splitSelectionOrAllAtPlayhead()
            pressedOnScissors = false
            panel.endPlayheadSeek()
        }
        onCanceled: {
            pressedOnScissors = false
            playhead.finishSeek()
            panel.endPlayheadSeek()
        }

        ThemedToolTip {
            text: qsTr("Cut at the bar — the selected clip, or every clip under it when nothing is selected")
            visible: playheadDragArea.containsMouse && !playheadDragArea.pressed
                     && playheadDragArea.mouseY < playhead.scissorsHitHeight
        }
    }

    // Narrow grab down the timeline line — stays thin so clip
    // clicks aren't stolen.
    MouseArea {
        id: playheadLineDrag
        width: 7
        height: parent.height - playheadDragArea.height
        x: -(width - Theme.playheadLineWidth) / 2
        y: playheadDragArea.height
        cursorShape: Qt.SizeHorCursor
        preventStealing: true
        drag.target: playhead
        drag.axis: Drag.XAxis
        drag.threshold: 0
        drag.minimumX: 0
        drag.maximumX: view.contentWidth - Theme.playheadLineWidth
        onPressed: panel.beginPlayheadSeek()
        onReleased: {
            playhead.finishSeek()
            panel.endPlayheadSeek()
        }
        onCanceled: {
            playhead.finishSeek()
            panel.endPlayheadSeek()
        }
    }
}
