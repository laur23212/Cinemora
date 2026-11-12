import QtQuick
import Drift

// Dafin: Filmora's Speed Ramping choices — None, Customize, then the presets (Montage, Hero,
// Bullet Time, Jumper, Flash in, Flash out), each tile drawing its curve. `current` is the id the
// selected clip's ramp matches ("none", a preset id, or "custom", which lights Customize).
Grid {
    id: grid

    property string current: "none"
    property var presets: []              // EditorState.speedRampPresets()
    signal picked(string id)

    columns: 4
    spacing: 8
    readonly property real tileWidth: Math.floor((width - spacing * (columns - 1)) / columns)

    Repeater {
        model: [{ id: "none", label: qsTr("None") }, { id: "custom", label: qsTr("Customize") }]
                   .concat(grid.presets)
        delegate: Column {
            id: tile
            required property var modelData
            readonly property bool selected: grid.current === modelData.id
            width: grid.tileWidth
            spacing: 4

            Rectangle {
                width: tile.width
                height: Math.round(tile.width * 0.78)
                radius: 6
                color: tileMouse.containsMouse ? Theme.panelAccent : Theme.panelSecondaryBg
                border.width: tile.selected ? 2 : 1
                border.color: tile.selected ? Theme.primary : Theme.panelBorder

                // None: a circle with a slash.
                Item {
                    visible: tile.modelData.id === "none"
                    anchors.centerIn: parent
                    width: Math.min(parent.width, parent.height) * 0.42
                    height: width
                    Rectangle {
                        anchors.fill: parent
                        radius: width / 2
                        color: "transparent"
                        border.width: 1.5
                        border.color: Theme.panelForeground
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        width: parent.width * 0.9
                        height: 1.5
                        rotation: -45
                        color: Theme.panelForeground
                    }
                }
                // Customize: three sliders.
                Column {
                    visible: tile.modelData.id === "custom"
                    anchors.centerIn: parent
                    spacing: 5
                    Repeater {
                        model: [0.65, 0.25, 0.5]
                        delegate: Item {
                            required property real modelData
                            width: tile.width * 0.42
                            height: 6
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width
                                height: 1.5
                                color: Theme.panelForeground
                            }
                            Rectangle {
                                x: parent.width * modelData - 3
                                width: 6
                                height: 6
                                radius: 3
                                color: Theme.panelBackground
                                border.width: 1.5
                                border.color: Theme.panelForeground
                            }
                        }
                    }
                }
                SpeedRampGraph {
                    visible: tile.modelData.id !== "none" && tile.modelData.id !== "custom"
                    anchors.fill: parent
                    anchors.margins: 4
                    points: tile.modelData.points || []
                    lineColor: Theme.panelForeground
                }

                MouseArea {
                    id: tileMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: grid.picked(tile.modelData.id)
                }
            }

            Text {
                width: tile.width
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: tile.modelData.label
                color: tile.selected ? Theme.panelForeground : Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }
    }
}
