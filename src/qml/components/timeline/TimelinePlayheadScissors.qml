import QtQuick
import Drift

// Dafin: Filmora's scissors on the playhead, at the foot of the ruler just above the tracks.
// Click: cut at the playhead (the selected clips it crosses, or everything under it) and the
// right-hand pieces stay selected, so moving on and clicking again keeps cutting the same clip.
// Drag sideways: move the playhead, as dragging its head does.
Item {
    id: knob

    property var panel
    property Flickable view
    // The TimelinePlayhead this rides on.
    property Item playhead

    readonly property bool scrubbing: knobArea.scrubbing
    readonly property bool hot: knobArea.containsMouse || knobArea.pressed

    width: 20
    height: 26
    x: -width / 2 + Theme.playheadLineWidth / 2
    // Pinned like the seek strip, so it stays put while the tracks scroll vertically.
    y: (view ? view.contentY : 0) + Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight / 2
       - height / 2 + 4
    z: 10

    // Side arrows on hover: the knob also drags.
    Repeater {
        model: [-1, 1]
        delegate: Canvas {
            required property int modelData
            width: 5
            height: 8
            anchors.verticalCenter: parent.verticalCenter
            x: modelData < 0 ? -width - 2 : knob.width + 2
            opacity: knob.hot ? 1 : 0
            Behavior on opacity {
                NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
            }
            readonly property color fill: Theme.c.timelinePlayheadHead.background
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.beginPath()
                if (modelData < 0) {
                    ctx.moveTo(width, 0)
                    ctx.lineTo(0, height / 2)
                    ctx.lineTo(width, height)
                } else {
                    ctx.moveTo(0, 0)
                    ctx.lineTo(width, height / 2)
                    ctx.lineTo(0, height)
                }
                ctx.closePath()
                ctx.fillStyle = fill
                ctx.fill()
            }
            onFillChanged: requestPaint()
            Component.onCompleted: requestPaint()
        }
    }

    Rectangle {
        id: pill
        anchors.fill: parent
        radius: width / 2
        color: Theme.c.timelinePlayheadHead.background
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, knob.hot ? 0.55 : 0.25)
        scale: knobArea.pressed && !knobArea.scrubbing ? 0.9 : (knob.hot ? 1.08 : 1)
        Behavior on scale {
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        IconGlyph {
            anchors.centerIn: parent
            glyph: Theme.icons.scissors
            iconSize: 13
            iconColor: "white"
            // Blades down, toward the tracks.
            rotation: 90
        }
    }

    MouseArea {
        id: knobArea
        anchors.fill: parent
        anchors.margins: -4
        hoverEnabled: true
        preventStealing: true
        cursorShape: scrubbing ? Qt.SizeHorCursor : Qt.PointingHandCursor

        property bool scrubbing: false
        property real pressSceneX: 0

        function contentX(mouse) {
            // The playhead's parent is the timeline content, which does not move under a drag.
            return knobArea.mapToItem(knob.playhead.parent, mouse.x, mouse.y).x
        }

        onPressed: (mouse) => {
            scrubbing = false
            pressSceneX = knobArea.mapToItem(null, mouse.x, mouse.y).x
            knob.panel.forceActiveFocus()
        }
        onPositionChanged: (mouse) => {
            if (!pressed)
                return
            if (!scrubbing) {
                if (Math.abs(knobArea.mapToItem(null, mouse.x, mouse.y).x - pressSceneX) < 4)
                    return
                scrubbing = true
                knob.panel.beginPlayheadSeek()
            }
            EditorState.playheadSeconds = Math.max(0, contentX(mouse)) / knob.panel.pxPerSecond
        }
        onReleased: (mouse) => {
            if (scrubbing) {
                EditorState.playheadSeconds = EditorState.snapTime(
                    Math.max(0, contentX(mouse)) / knob.panel.pxPerSecond)
                knob.panel.endPlayheadSeek()
                scrubbing = false
                return
            }
            EditorState.splitAtPlayheadKnob()
        }
        onCanceled: {
            if (scrubbing)
                knob.panel.endPlayheadSeek()
            scrubbing = false
        }
        // Scrolling over the knob scrolls (or Ctrl-zooms) the timeline under it.
        onWheel: (wheel) => { wheel.accepted = false }
    }

    ThemedToolTip {
        text: qsTr("Split here — drag to move the playhead")
        visible: knobArea.containsMouse && !knobArea.pressed
    }
}
