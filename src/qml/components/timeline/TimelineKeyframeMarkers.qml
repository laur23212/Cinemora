import QtQuick
import Drift

// Dafin: a clip's keyframes as diamonds along its bottom edge, the way Filmora shows them.
//
//   click a diamond          select the clip and move the playhead onto that keyframe
//   drag a diamond           retime it: everything keyed at that moment moves together
//   right-click a diamond    Delete keyframe / Clear all keyframes
//
// One diamond stands for every property keyed at that moment (position, scale, rotation,
// opacity, effects…); volume keys are left to the volume line. A diamond under the playhead is
// lit, like the toolbar's keyframe button. Presses away from a diamond fall through to the clip
// (select, move, trim) and to the volume line, so the clip handles exactly as before.
Item {
    id: root

    required property var owner                 // TimelineClipItem

    readonly property var clip: owner.clipData
    readonly property var keyTimes: clip.keyTimes || []
    readonly property real clipStart: owner.effectiveStart
    readonly property real clipDuration: Math.max(0.001, owner.effectiveDuration)
    readonly property real size: 9                // the square before it is turned 45°
    readonly property real hitSlop: 5
    readonly property real centreY: height - size * 0.71 - 3
    readonly property real playheadRel: EditorState.playheadSeconds - clipStart
    readonly property real tolerance: 1 / 30
    property int menuIndex: -1
    property real menuTime: 0

    anchors.fill: parent
    visible: keyTimes.length > 0 && !owner.touchMode && width >= 12 && height >= 16

    function xForTime(t) { return t * width / clipDuration }
    function timeForX(x) { return Math.max(0, Math.min(clipDuration, x / width * clipDuration)) }
    function indexAt(x, y) {
        if (Math.abs(y - centreY) > size * 0.71 + hitSlop)
            return -1
        let best = -1
        let bestD = size * 0.71 + hitSlop
        for (let i = 0; i < keyTimes.length; ++i) {
            const d = Math.abs(xForTime(keyTimes[i]) - x)
            if (d <= bestD) {
                best = i
                bestD = d
            }
        }
        return best
    }
    function formatTime(seconds) {
        const panel = owner.panel
        return panel && panel.formatTime ? panel.formatTime(seconds) : seconds.toFixed(2) + " s"
    }

    // A faint guide the diamonds sit on, so a run of keyframes reads as one strip.
    Rectangle {
        visible: root.keyTimes.length > 1
        x: root.keyTimes.length > 0 ? root.xForTime(root.keyTimes[0]) : 0
        width: root.keyTimes.length > 1 ? Math.max(0, root.xForTime(root.keyTimes[root.keyTimes.length - 1]) - x) : 0
        y: root.centreY
        height: 1
        color: Qt.rgba(1, 1, 1, 0.35)
    }

    Repeater {
        model: root.keyTimes
        delegate: Rectangle {
            id: diamond
            required property var modelData
            required property int index
            readonly property bool dragged: drag.moving && drag.index === index
            readonly property bool hot: dragged || hover.index === index || root.menuIndex === index
            readonly property bool current: Math.abs(modelData - root.playheadRel) <= root.tolerance + 1e-6
            x: (dragged ? root.xForTime(drag.time) : root.xForTime(modelData)) - width / 2
            y: root.centreY - height / 2
            width: hot ? root.size + 2 : root.size
            height: width
            rotation: 45
            antialiasing: true
            radius: 1
            color: current || hot ? Theme.primary : "#f2f2f2"
            border.width: 1
            border.color: current || hot ? "white" : "#1e1e1e"
        }
    }

    // Time readout while dragging.
    Rectangle {
        visible: drag.moving
        x: Math.max(0, Math.min(root.width - width, root.xForTime(drag.time) - width / 2))
        y: Math.max(0, root.centreY - height - 10)
        width: readout.implicitWidth + 8
        height: readout.implicitHeight + 4
        radius: 3
        color: "#d0000000"
        z: 2
        Text {
            id: readout
            anchors.centerIn: parent
            text: root.formatTime(root.clipStart + drag.time)
            color: "white"
            font.pixelSize: Theme.fontSizeXs
        }
    }

    // Passive, so the clip underneath keeps its own hover state.
    HoverHandler {
        id: hover
        property int index: hovered ? root.indexAt(point.position.x, point.position.y) : -1
        // undefined resets it, so away from a diamond the clip keeps its own cursor.
        cursorShape: index >= 0 ? Qt.SizeHorCursor : undefined
    }

    MouseArea {
        id: drag
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        preventStealing: true

        property int index: -1
        property bool moving: false
        property bool previewOpen: false
        property real origin: 0     // the keyframe's time at the press, clip seconds
        property real time: 0       // where it is being dragged to, clip seconds
        property real pressX: 0
        property var refs: []

        function reset() {
            moving = false
            previewOpen = false
            index = -1
            refs = []
        }

        onPressed: (mouse) => {
            const hit = root.indexAt(mouse.x, mouse.y)
            if (hit < 0) {
                mouse.accepted = false   // the clip below selects / moves / trims as usual
                return
            }
            if (mouse.button === Qt.RightButton) {
                root.menuIndex = hit
                root.menuTime = root.keyTimes[hit]
                keyMenu.popup()
                return
            }
            index = hit
            origin = root.keyTimes[hit]
            time = origin
            pressX = mouse.x
            refs = EditorState.clipKeyframeRefsAt(owner.trackIndex, owner.clipIndex, root.clipStart + origin)
        }

        onPositionChanged: (mouse) => {
            if (index < 0 || !pressed)
                return
            if (!moving && Math.abs(mouse.x - pressX) < Qt.styleHints.startDragDistance)
                return
            moving = true
            // Whole frames, like every other edit on the timeline.
            const fps = Math.max(1, EditorState.projectFile.projectFps())
            let t = root.timeForX(mouse.x)
            const absolute = Math.round((root.clipStart + t) * fps) / fps
            t = Math.max(0, Math.min(root.clipDuration, absolute - root.clipStart))
            time = t
            if (refs.length === 0)
                return
            if (!previewOpen) {
                EditorState.beginPreviewDrag(qsTr("Move keyframe"))
                previewOpen = true
            }
            EditorState.keyframes.previewMoveKeys(owner.trackIndex, owner.clipIndex, refs, t - origin)
        }

        onReleased: {
            if (index < 0)
                return
            if (moving) {
                if (previewOpen) {
                    EditorState.commitPreviewDrag()
                    EditorState.keyframes.clearCollisions()
                }
            } else {
                // A click: work on that keyframe, as clicking one in Filmora does.
                EditorState.selectClip(owner.trackIndex, owner.clipIndex)
                EditorState.playheadSeconds = root.clipStart + origin
            }
            reset()
        }

        onCanceled: {
            if (previewOpen) {
                EditorState.cancelPreviewDrag()
                EditorState.keyframes.clearCollisions()
            }
            reset()
        }
    }

    ThemedContextMenu {
        id: keyMenu
        onClosed: root.menuIndex = -1

        ThemedMenuItem {
            text: qsTr("Go to keyframe")
            icon.name: Theme.icons.locateFixed
            onTriggered: {
                EditorState.selectClip(owner.trackIndex, owner.clipIndex)
                EditorState.playheadSeconds = root.clipStart + root.menuTime
            }
        }
        ThemedMenuSeparator { }
        ThemedMenuItem {
            text: qsTr("Delete keyframe")
            icon.name: Theme.icons.diamondMinus
            onTriggered: EditorState.removeClipKeyframesAt(owner.trackIndex, owner.clipIndex,
                                                           root.clipStart + root.menuTime)
        }
        ThemedMenuItem {
            text: qsTr("Clear all keyframes")
            icon.name: Theme.icons.trash
            onTriggered: EditorState.clearClipKeyframes(owner.trackIndex, owner.clipIndex)
        }
    }
}
