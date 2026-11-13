import QtQuick
import QtQuick.Controls.Basic
import Drift

// Pinned ruler and bookmark strip. Stays under the playhead head so a drag on the
// head wins, and follows vertical scroll so it never leaves the viewport.
Item {
    id: seekStrip

    property var panel
    property Flickable view
    property bool playheadDragging: false
    property var renameDialog
    property var renameField
    property alias bookmarkRenameIndex: bookmarkRow.renameIndex

    width: parent.width
    y: view.contentY
    z: 2
    height: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight

    TimelineSeekStripFill { }
    TimelineSeekStripEdge { }

    MouseArea {
        id: rulerScrub
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.SizeHorCursor
        preventStealing: true
        // Leave room for the playhead head so its drag can start first.
        z: 1

        // Filmora-style: a click puts the bar there; holding and dragging sideways zooms the
        // timeline around the bar (right = zoom in, left = zoom out). Dragging the head still scrubs.
        readonly property real zoomDragStartPx: 6
        readonly property real zoomPxPerDoubling: 120
        property real pressPanelX: 0
        property real startZoom: 1
        property real anchorSeconds: 0
        property real anchorViewportX: 0
        property bool zooming: false

        function scrubTo(x) {
            EditorState.playheadSeconds =
                EditorState.snapTime(Math.max(0, x) / seekStrip.panel.pxPerSecond)
        }

        // Seeking is not a selection change: scrubbing to look at a
        // different point used to drop the clip you were editing.
        onPressed: (mouse) => {
            seekStrip.panel.forceActiveFocus()
            seekStrip.panel.beginPlayheadSeek()
            scrubTo(mouse.x)
            pressPanelX = mapToItem(seekStrip.panel, mouse.x, mouse.y).x
            startZoom = seekStrip.panel.zoom
            anchorSeconds = Math.max(0, EditorState.playheadSeconds)
            anchorViewportX = anchorSeconds * seekStrip.panel.pxPerSecond - seekStrip.view.contentX
            zooming = false
        }
        onPositionChanged: (mouse) => {
            if (!pressed)
                return
            const dx = mapToItem(seekStrip.panel, mouse.x, mouse.y).x - pressPanelX
            if (!zooming && Math.abs(dx) >= zoomDragStartPx)
                zooming = true
            if (zooming)
                seekStrip.panel.setZoomAround(startZoom * Math.pow(2, dx / zoomPxPerDoubling),
                                              anchorSeconds, anchorViewportX)
        }
        onReleased: {
            zooming = false
            seekStrip.panel.endPlayheadSeek()
        }
        onCanceled: {
            zooming = false
            seekStrip.panel.endPlayheadSeek()
        }
        // MouseArea would otherwise swallow wheel and block Ctrl-zoom.
        onWheel: (wheel) => seekStrip.panel.handleTimelineWheel(wheel)

        ThemedToolTip {
            text: qsTr("Click to move the bar. Hold and drag left or right to zoom the timeline")
            visible: rulerScrub.containsMouse && !rulerScrub.pressed
                     && !seekStrip.playheadDragging
        }
    }

    TimelineRuler {
        panel: seekStrip.panel
        view: seekStrip.view
    }

    Item {
        id: bookmarkRow
        y: Theme.timelineRulerHeight
        width: parent.width
        height: Theme.timelineBookmarkRowHeight
        z: 2

        property int renameIndex: -1

        Repeater {
            model: EditorState.bookmarks
            delegate: TimelineBookmarkMarker {
                row: bookmarkRow
                panel: seekStrip.panel
                onContextMenu: (atPointer, anchor, ax, ay, bookmarkIndex, label, seconds, color) => {
                    bookmarkContextMenu.bookmarkIndex = bookmarkIndex
                    bookmarkContextMenu.bookmarkLabel = label
                    bookmarkContextMenu.bookmarkSeconds = seconds
                    bookmarkContextMenu.bookmarkColor = color
                    if (atPointer)
                        bookmarkContextMenu.popup(anchor, ax, ay)
                    else
                        bookmarkContextMenu.popup()
                }
                onRenameRequested: (bookmarkIndex, label) => seekStrip.openRename(bookmarkIndex, label)
            }
        }

        TimelineBookmarkContextMenu {
            id: bookmarkContextMenu
            onRenameRequested: (index, label) => seekStrip.openRename(index, label)
        }
    }

    function openRename(index, label) {
        bookmarkRow.renameIndex = index
        renameField.text = label
        renameDialog.open()
    }

    TimelineBookmarkLaneHighlight {
        pxPerSecond: panel.pxPerSecond
    }
}
