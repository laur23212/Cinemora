import QtQuick
import QtQuick.Window

// Makes the item it is placed in draggable within its bar: press and hold for half a second,
// then drag it to a new place and let go. Put it inside the item (it fills its parent) and
// point `content` at the part that handles clicks; that part is switched off for the length
// of the drag, so letting go never also clicks it. A press that moves before the half second
// is an ordinary press or drag (a slider still slides).
//
// A PointHandler only watches the pointer: the button underneath still gets every press,
// hover and click as before.
Item {
    id: handle

    property BarReorder reorder
    property string group
    property string itemId
    // The clickable part to disable while dragging; defaults to the parent.
    property Item content: parent
    property bool draggable: true
    // How far the pointer may wander during the hold before it counts as a normal drag.
    property real slop: 6

    anchors.fill: parent
    z: 1000

    readonly property bool beingDragged: reorder !== null && reorder.dragging && reorder.dragId === itemId

    property point _pressScenePos
    property point _lastScenePos

    // The item stays in place, faded, while its ghost follows the pointer.
    Binding {
        target: handle.parent
        property: "opacity"
        value: 0.3
        when: handle.beingDragged
    }
    Binding {
        target: handle.content
        property: "enabled"
        value: false
        when: handle.beingDragged
    }

    Timer {
        id: hold
        interval: 500
        onTriggered: {
            if (!point.active || !reorder || reorder.dragging)
                return
            const source = handle.parent
            const dpr = Math.max(1, Screen.devicePixelRatio)
            // Snapshot first, while the item still looks normal; the drag starts once it exists.
            const grabbing = source.grabToImage(function (result) {
                if (point.active && !reorder.dragging)
                    reorder.begin(handle.itemId, handle.group, source, handle._lastScenePos, result.url)
            }, Qt.size(Math.ceil(source.width * dpr), Math.ceil(source.height * dpr)))
            if (!grabbing)
                reorder.begin(handle.itemId, handle.group, source, handle._lastScenePos, "")
        }
    }

    PointHandler {
        id: point
        enabled: handle.draggable && handle.reorder !== null && handle.itemId !== ""
        acceptedButtons: Qt.LeftButton
        onActiveChanged: {
            if (active) {
                handle._pressScenePos = point.point.scenePosition
                handle._lastScenePos = handle._pressScenePos
                hold.restart()
            } else {
                hold.stop()
                if (handle.beingDragged)
                    reorder.end(true)
            }
        }
        onPointChanged: {
            if (!active)
                return
            const p = point.point.scenePosition
            handle._lastScenePos = p
            if (handle.beingDragged) {
                reorder.move(p)
            } else if (hold.running
                       && (Math.abs(p.x - handle._pressScenePos.x) > handle.slop
                           || Math.abs(p.y - handle._pressScenePos.y) > handle.slop)) {
                hold.stop()
            }
        }
        onCanceled: {
            hold.stop()
            if (handle.beingDragged)
                reorder.end(false)
        }
    }

    // Esc puts it back where it was.
    Shortcut {
        enabled: handle.beingDragged
        sequence: "Escape"
        onActivated: handle.reorder.end(false)
    }
}
