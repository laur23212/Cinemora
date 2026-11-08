pragma ComponentBehavior: Bound
import QtQuick
import Drift

// Dafin motion tracking on the preview (MotionTrackingFlow arms it). While ShellBridge.motionPick
// is set, a click on the video picks the point the elements follow; pressing and dragging sizes the
// box that is tracked, for a large or small feature. In "correct" mode the click moves the point at
// the playhead instead, and the point as tracked so far is drawn. Esc, or the button again, cancels.
// While tracking runs in the background a small badge shows how far it has got.
Item {
    id: root

    // The canvas rect inside the viewport this overlay fills (PreviewViewport.canvas).
    property Item canvas

    readonly property var pick: ShellBridge.motionPick
    readonly property bool picking: !!pick

    // trackerFrameOf() for the video the point is picked in.
    property var frame: ({})
    readonly property bool hasFrame: picking && !!frame.hasFrame
    // In "correct" mode, the tracker whose point is shown.
    property string trackerId: ""
    property string hostClipId: ""

    property bool pressed: false
    property point pressAt: Qt.point(0, 0)
    property point pointerAt: Qt.point(-1, -1)

    readonly property real canvasW: Math.max(1, frame.canvasWidth || 1)
    readonly property real canvasH: Math.max(1, frame.canvasHeight || 1)
    readonly property real sx: canvasItem.width / canvasW
    readonly property real sy: canvasItem.height / canvasH
    // The default tracked box, as TrackingController::buildPointTracker makes it.
    readonly property real defaultBox: clipFrame.width * 0.05
    readonly property real dragBox: 2 * Math.max(Math.abs(pointerAt.x - pressAt.x), Math.abs(pointerAt.y - pressAt.y))
    readonly property bool sizing: pressed && dragBox >= 8

    function refresh() {
        // Read through ShellBridge, not `picking`: in onPickChanged that binding may not have
        // caught up yet.
        const current = ShellBridge.motionPick
        if (!current) {
            frame = ({})
            trackerId = ""
            hostClipId = ""
            return
        }
        hostClipId = current.hostClipId
        const ref = EditorState.tracking.clipRefById(current.hostClipId)
        frame = ref.track !== undefined ? EditorState.preview.trackerFrameOf(ref.track, ref.clip) : ({})
        if (current.mode === "correct" && current.followerIds.length > 0) {
            const info = EditorState.tracking.motionInfo(current.followerIds[0])
            trackerId = info.following ? info.trackerId : ""
        } else {
            trackerId = ""
        }
        markers.seconds = EditorState.playheadSeconds
        markers.refresh()
    }

    function finish() {
        const current = ShellBridge.motionPick
        if (!current)
            return
        // Read before the pick is cleared: that resets the press.
        const x = Math.max(0, Math.min(1, root.pressAt.x / Math.max(1, clipFrame.width)))
        const y = Math.max(0, Math.min(1, root.pressAt.y / Math.max(1, clipFrame.height)))
        const box = root.sizing ? root.dragBox / Math.max(1, clipFrame.width) : -1
        ShellBridge.motionPick = null
        if (current.mode === "correct") {
            EditorState.tracking.correctMotionPoint(current.followerIds[0], x, y)
            return
        }
        const ids = EditorState.tracking.startMotionFollow(current.hostClipId, current.followerIds, x, y, box)
        if (ids.length > 0)
            Toasts.info(qsTr("Tracking “%1”. It keeps following the point as you move, trim or extend it.")
                        .arg(current.label))
    }

    function hintText() {
        const current = root.pick
        if (!current)
            return ""
        if (!root.hasFrame)
            return qsTr("Move the playhead over “%1” to pick the point · Esc cancels").arg(current.hostName)
        if (current.mode === "correct")
            return qsTr("Click where the point is now · Esc cancels")
        return qsTr("Click the spot in “%1” that “%2” should follow · drag to size the box · Esc cancels")
               .arg(current.hostName).arg(current.label)
    }

    onPickChanged: {
        pressed = false
        refresh()
    }
    Component.onCompleted: refresh()

    Connections {
        target: EditorState
        enabled: root.picking
        function onTracksChanged() { root.refresh() }
        function onPlayheadSecondsChanged() {
            if (!EditorState.playing)
                root.refresh()
        }
        function onPlayingChanged() { root.refresh() }
    }

    Connections {
        target: EditorState.tracking
        enabled: root.picking && root.trackerId !== ""
        function onSamplesUpdated(sampleTrackerId) {
            if (sampleTrackerId === root.trackerId)
                root.refresh()
        }
    }

    // The canvas, in the viewport's coordinates.
    Item {
        id: canvasItem
        x: root.canvas ? root.canvas.x : 0
        y: root.canvas ? root.canvas.y : 0
        width: root.canvas ? root.canvas.width : 0
        height: root.canvas ? root.canvas.height : 0

        // The video's destination rect on the canvas, rotated with it, as TrackerOverlay places it.
        Item {
            id: clipFrame
            visible: root.hasFrame
            x: (root.frame.x || 0) * root.sx
            y: (root.frame.y || 0) * root.sy
            width: Math.max(1, (root.frame.width || root.canvasW) * root.sx)
            height: Math.max(1, (root.frame.height || root.canvasH) * root.sy)
            transformOrigin: Item.Center
            readonly property bool is3d: (root.frame.rotationX || 0) !== 0 || (root.frame.rotationY || 0) !== 0
                                         || (root.frame.z || 0) !== 0
                                         || root.frame.anchorX !== undefined || root.frame.anchorY !== undefined
            readonly property bool hasParent: root.frame.parentActive === true
            rotation: is3d || hasParent ? 0 : (root.frame.rotation || 0)
            transform: Matrix4x4 {
                matrix: clipFrame.is3d || clipFrame.hasParent
                        ? EditorState.preview.clipPoseMatrix(root.frame, root.frame.x || 0, root.frame.y || 0,
                                                            root.frame.width || root.canvasW,
                                                            root.frame.height || root.canvasH,
                                                            root.frame.rotation || 0, root.sx, root.sy)
                        : Qt.matrix4x4()
            }

            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.width: Theme.c.maskDrawOverlay.borderWidth
                border.color: Theme.c.trackerOverlay.region
                opacity: 0.6
            }

            TrackerOverlayItem {
                id: markers
                anchors.fill: parent
                visible: root.trackerId !== ""
                tracking: EditorState.tracking
                clipId: root.trackerId !== "" ? root.hostClipId : ""
                trackerId: root.trackerId
                minConfidence: 0.5
                showTrails: true
                lineWidth: Theme.c.maskDrawOverlay.borderWidth
                handleSize: Theme.spacingLg
                handleMargin: Theme.spacingMd
                patternColor: Theme.c.trackerOverlay.pattern
                trailColor: Theme.c.trackerOverlay.trail
                goodColor: Theme.c.trackerOverlay.good
                warnColor: Theme.c.trackerOverlay.warn
                badColor: Theme.c.trackerOverlay.bad
                userKeyColor: Theme.c.trackerOverlay.userKey
                regionColor: Theme.c.trackerOverlay.region
                handleColor: Theme.c.trackerOverlay.handle
            }

            // The box that will be tracked: under the pointer, or sized by the drag.
            Rectangle {
                readonly property real side: root.sizing ? root.dragBox : root.defaultBox
                readonly property point centre: root.pressed ? root.pressAt : root.pointerAt
                visible: !!root.pick && root.pick.mode !== "correct" && (root.pressed || pickArea.containsMouse)
                x: centre.x - side / 2
                y: centre.y - side / 2
                width: side
                height: side
                color: "transparent"
                border.width: Theme.c.maskDrawOverlay.borderWidth * 2
                border.color: Theme.c.trackerOverlay.pattern
            }

            MouseArea {
                id: pickArea
                anchors.fill: parent
                enabled: root.hasFrame
                hoverEnabled: true
                preventStealing: true
                acceptedButtons: Qt.LeftButton
                onWheel: (wheel) => { wheel.accepted = false }
                onPositionChanged: (mouse) => { root.pointerAt = Qt.point(mouse.x, mouse.y) }
                onPressed: (mouse) => {
                    root.pressAt = Qt.point(mouse.x, mouse.y)
                    root.pointerAt = root.pressAt
                    root.pressed = true
                }
                onReleased: (mouse) => {
                    root.pointerAt = Qt.point(mouse.x, mouse.y)
                    root.finish()
                    root.pressed = false
                }
                onCanceled: root.pressed = false
            }

            ToolCursor {
                target: pickArea
                shape: "trackPoint"
            }
        }

        // Background motion tracking's progress.
        Rectangle {
            visible: EditorState.tracking.busyAuto
            x: Theme.spacingSm
            y: Theme.spacingSm
            width: badgeText.implicitWidth + Theme.spacingMd * 2
            height: badgeText.implicitHeight + Theme.spacingXs * 2
            radius: Theme.radiusXs
            color: Theme.c.trackerOverlay.badgeBackground
            opacity: 0.92

            Text {
                id: badgeText
                anchors.centerIn: parent
                text: qsTr("Motion tracking… %1%").arg(Math.round(EditorState.tracking.progress * 100))
                color: Theme.c.trackerOverlay.badgeForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }
    }

    // Everything outside the video does not take the click while picking, but cancels nothing
    // either: a stray click beside the frame should not lose the pick.
    MouseArea {
        anchors.fill: parent
        z: -1
        enabled: root.picking
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: (mouse) => {
            if (mouse.button === Qt.RightButton)
                ShellBridge.motionPick = null
        }
        onWheel: (wheel) => { wheel.accepted = false }
    }

    Rectangle {
        visible: hintLabel.text !== ""
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacingLg
        width: Math.min(parent.width - Theme.spacingLg * 2, hintLabel.implicitWidth + Theme.spacingLg * 2)
        height: hintLabel.height + Theme.spacingMd * 2
        radius: Theme.c.maskDrawOverlay.radius
        color: Theme.c.maskDrawOverlay.hintBackground

        Text {
            id: hintLabel
            anchors.centerIn: parent
            width: Math.min(implicitWidth, parent.width - Theme.spacingLg * 2)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: root.hintText()
            color: Theme.c.maskDrawOverlay.hintForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }
}
