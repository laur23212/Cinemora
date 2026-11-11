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
        // Dafin: Filmora's ruler. On the time labels a sideways drag zooms the timeline (right
        // in, left out) with the playhead held still, and a click seeks; the bookmark lane under
        // it scrubs as before, and so do the playhead's head and its scissors knob.
        cursorShape: zooming || (containsMouse && inRuler(mouseY)) ? Qt.SizeHorCursor
                                                                   : Qt.PointingHandCursor
        preventStealing: true
        // Leave room for the playhead head so its drag can start first.
        z: 1

        property bool rulerPress: false
        property bool zooming: false
        property real pressViewX: 0
        property real pressZoom: 1
        property real anchorSeconds: 0
        property real anchorViewX: 0

        function inRuler(y) { return y < Theme.timelineRulerHeight }
        function viewX(mouse) { return rulerScrub.mapToItem(seekStrip.view, mouse.x, mouse.y).x }

        function scrubTo(x) {
            EditorState.playheadSeconds =
                EditorState.snapTime(Math.max(0, x) / seekStrip.panel.pxPerSecond)
        }

        // Seeking is not a selection change: scrubbing to look at a
        // different point used to drop the clip you were editing.
        onPressed: (mouse) => {
            seekStrip.panel.forceActiveFocus()
            rulerPress = inRuler(mouse.y)
            zooming = false
            if (rulerPress) {
                // Nothing moves until it is clear whether this is a click or a zoom drag.
                pressViewX = viewX(mouse)
                pressZoom = seekStrip.panel.zoom
                anchorSeconds = Math.max(0, EditorState.playheadSeconds)
                anchorViewX = anchorSeconds * seekStrip.panel.pxPerSecond - seekStrip.view.contentX
                if (anchorViewX < 0 || anchorViewX > seekStrip.view.width) {
                    // Playhead out of sight: zoom about the point that was pressed instead.
                    anchorViewX = pressViewX
                    anchorSeconds = Math.max(0, (pressViewX + seekStrip.view.contentX)
                                                / seekStrip.panel.pxPerSecond)
                }
                return
            }
            seekStrip.panel.beginPlayheadSeek()
            scrubTo(mouse.x)
        }
        onPositionChanged: (mouse) => {
            if (!pressed)
                return
            if (!rulerPress) {
                scrubTo(mouse.x)
                return
            }
            const dx = viewX(mouse) - pressViewX
            if (!zooming && Math.abs(dx) < 4)
                return
            zooming = true
            // Twice as close every 120 px of drag.
            seekStrip.panel.setZoomAround(pressZoom * Math.pow(2, dx / 120), anchorSeconds, anchorViewX)
        }
        onReleased: (mouse) => {
            if (rulerPress) {
                if (!zooming) {
                    seekStrip.panel.beginPlayheadSeek()
                    scrubTo(mouse.x)
                    seekStrip.panel.endPlayheadSeek()
                }
                rulerPress = false
                zooming = false
                return
            }
            seekStrip.panel.endPlayheadSeek()
        }
        onCanceled: {
            if (!rulerPress)
                seekStrip.panel.endPlayheadSeek()
            rulerPress = false
            zooming = false
        }
        // MouseArea would otherwise swallow wheel and block Ctrl-zoom.
        onWheel: (wheel) => seekStrip.panel.handleTimelineWheel(wheel)

        ThemedToolTip {
            text: rulerScrub.inRuler(rulerScrub.mouseY)
                  ? qsTr("Click to seek — drag sideways to zoom")
                  : qsTr("Click or drag to seek")
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
                onContextMenu: (atPointer, anchor, ax, ay, bookmarkIndex, label, seconds) => {
                    bookmarkContextMenu.bookmarkIndex = bookmarkIndex
                    bookmarkContextMenu.bookmarkLabel = label
                    bookmarkContextMenu.bookmarkSeconds = seconds
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
