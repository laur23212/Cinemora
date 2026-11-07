import QtQuick
import QtQuick.Layouts
import Drift

// The strip under the preview that is always there: timecode, transport and fullscreen. Everything
// else lives in the preview's OSD. A RowLayout keeps the transport centred while the side groups
// compress instead of colliding at narrow widths. Every control can be moved by long-press dragging.
Item {
    id: bar

    property int projectFps: 30
    property real currentSeconds: 0
    property real durationSeconds: 0
    property bool fullscreen: false
    // Formats seconds as the panel's timecode.
    property var formatTimecode: function (seconds) { return "" }
    signal fullscreenRequested()

    function withShortcut(label, actionId) {
        const key = EditorState.shortcutFor(actionId)
        return key.length > 0 ? qsTr("%1 (%2)").arg(label).arg(key) : label
    }

    implicitHeight: Theme.iconButtonSize + Theme.previewTransportPadding * 2

    ThemeSlot {
        anchors.fill: parent
        z: -1
        slot: "PreviewTransportBarBackground"
        control: bar
        fallback: Component {
            Rectangle {
                anchors.fill: parent
                color: Theme.c.previewTransportBar.background
            }
        }
    }

    // Long-press any control and drag it to rearrange the strip (see BarReorder).
    BarReorder {
        id: transportReorder
        defaults: ({
            "start": ["timecode"],
            "transport": ["goToStart", "rewind", "stepBack", "play", "stepForward", "loop",
                          "fastForward", "goToEnd"],
            "end": ["fullscreen"]
        })
        stored: EditorState.preferences.barLayouts["transport"]
        layer: reorderLayer
        onSaveRequested: (layout) => EditorState.preferences.setBarLayout("transport", layout)
    }

    readonly property var barItems: ({
        "timecode": timecodeComponent,
        "goToStart": goToStartComponent,
        "rewind": rewindComponent,
        "stepBack": stepBackComponent,
        "play": playComponent,
        "stepForward": stepForwardComponent,
        "loop": loopComponent,
        "fastForward": fastForwardComponent,
        "goToEnd": goToEndComponent,
        "fullscreen": fullscreenComponent
    })

    Component {
        id: timecodeComponent
        PreviewTransportTimecode {
            projectFps: bar.projectFps
            currentSeconds: bar.currentSeconds
            durationSeconds: bar.durationSeconds
            formatTimecode: bar.formatTimecode
        }
    }
    Component {
        id: goToStartComponent
        PreviewTransportGoToStartButton {
            shortcutTip: bar.withShortcut(qsTr("Go to start"), "goToStart")
        }
    }
    Component { id: rewindComponent; PreviewTransportRewindButton { } }
    Component {
        id: stepBackComponent
        PreviewTransportStepBackButton {
            shortcutTip: bar.withShortcut(qsTr("Previous frame"), "stepBack")
        }
    }
    Component { id: playComponent; PreviewTransportPlayButton { } }
    Component {
        id: stepForwardComponent
        PreviewTransportStepForwardButton {
            shortcutTip: bar.withShortcut(qsTr("Next frame"), "stepForward")
        }
    }
    Component { id: loopComponent; PreviewTransportLoopButton { } }
    Component { id: fastForwardComponent; PreviewTransportFastForwardButton { } }
    Component {
        id: goToEndComponent
        PreviewTransportGoToEndButton {
            shortcutTip: bar.withShortcut(qsTr("Go to end"), "goToEnd")
        }
    }
    Component {
        id: fullscreenComponent
        PreviewTransportFullscreenButton {
            fullscreen: bar.fullscreen
            onFullscreenRequested: bar.fullscreenRequested()
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing2xl + Theme.spacingSm
        anchors.rightMargin: Theme.spacing2xl + Theme.spacingSm
        spacing: Theme.spacingLg

        Row {
            id: startRow
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredHeight: bar.height
            spacing: Theme.spacingXs
            Component.onCompleted: transportReorder.registerGroup("start", startRow)
            Repeater {
                model: transportReorder.layout["start"]
                delegate: BarSlot { reorder: transportReorder; group: "start"; items: bar.barItems }
            }
        }

        // Spacers on both sides keep the transport optically centred while still letting the
        // whole row shrink instead of overlap.
        Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }

        Row {
            id: transportRow
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredHeight: bar.height
            spacing: Theme.spacingXs
            Component.onCompleted: transportReorder.registerGroup("transport", transportRow)
            Repeater {
                model: transportReorder.layout["transport"]
                delegate: BarSlot { reorder: transportReorder; group: "transport"; items: bar.barItems }
            }
        }

        Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }

        Row {
            id: endRow
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredHeight: bar.height
            spacing: Theme.spacingXs
            Component.onCompleted: transportReorder.registerGroup("end", endRow)
            Repeater {
                model: transportReorder.layout["end"]
                delegate: BarSlot { reorder: transportReorder; group: "end"; items: bar.barItems }
            }
        }
    }

    BarReorderLayer {
        id: reorderLayer
        reorder: transportReorder
    }
}
