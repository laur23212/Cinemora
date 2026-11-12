import QtQuick
import Drift

// Dafin: the speed strip across a clip, as in Filmora — "Normal 1.00×" in blue once right-click ›
// Speed › Show Speed Control is on, "Fast 3.00×" / "Slow 0.50×" in orange, "Ramp: Hero" in violet,
// "Freeze 5.0 s" in ice blue. Retimed clips keep it so their speed is always in view.
//
// While Show Speed Control is on, dragging either end of the clip changes its speed instead of
// trimming it (TimelineTrimHandle); the strip follows the drag live. Its ▾ opens quick speeds.
// Only the label takes clicks: the rest of the strip lets presses reach the clip below (select,
// move), like the rest of this overlay.
Item {
    id: strip

    required property var owner                 // TimelineClipItem

    readonly property string kind: owner.speedDragActive ? owner.speedDragKind : (owner.clipData.speedKind || "")
    readonly property string label: owner.speedDragActive ? owner.speedDragLabel : (owner.clipData.speedLabel || "")
    readonly property real bandHeight: Math.min(20, Math.max(0, owner.height * 0.5))

    x: 0
    y: Math.round(bandHeight)
    width: parent.width
    height: 14
    visible: kind !== "" && owner.width >= 28 && owner.height >= 26
    z: 20

    readonly property color fill: kind === "normal" ? "#2f9be0"
                                  : kind === "ramp" ? "#8c6ff0"
                                  : kind === "freeze" ? "#7cc8e8"
                                  : "#f08a4b"     // fast, slow

    Rectangle {
        anchors.fill: parent
        color: strip.fill
        opacity: strip.owner.selected || strip.owner.speedDragActive ? 0.95 : 0.8
    }

    // The label and its ▾; a click opens the quick speed menu.
    Row {
        id: labelRow
        anchors.centerIn: parent
        spacing: 3
        Text {
            text: strip.label
            color: strip.kind === "freeze" ? "#10202a" : "white"
            font.family: Theme.fontFamily
            font.pixelSize: 10
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            width: Math.min(implicitWidth, Math.max(0, strip.width - 22))
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            visible: strip.kind !== "freeze" && strip.width >= 60
            text: "▾"
            color: strip.kind === "freeze" ? "#10202a" : "white"
            font.pixelSize: 9
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        x: labelRow.x - 4
        y: 0
        width: labelRow.width + 8
        height: strip.height
        enabled: strip.kind !== "freeze" && !strip.owner.speedDragActive
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton
        onClicked: {
            if (!strip.owner.selected)
                EditorState.selectClip(strip.owner.trackIndex, strip.owner.clipIndex)
            if (quickMenuLoader.active && quickMenuLoader.item)
                quickMenuLoader.item.popup()
            else
                quickMenuLoader.active = true   // onLoaded pops it
        }
    }

    Loader {
        id: quickMenuLoader
        active: false
        sourceComponent: Component {
            ThemedContextMenu {
                id: quickMenu
                readonly property var clipOwner: strip.owner
                readonly property bool media: clipOwner.clipData.kind === "video"
                                               || clipOwner.clipData.kind === "audio"
                function setSpeed(value) {
                    // A ramp would go on overriding the uniform speed, so it comes off first.
                    if (strip.kind === "ramp")
                        EditorState.applySpeedRampPreset(clipOwner.trackIndex, clipOwner.clipIndex, "none")
                    EditorState.setClipSpeed(clipOwner.trackIndex, clipOwner.clipIndex, value)
                }
                function openPage(page) {
                    ShellBridge.speedPageRequest = page
                    Qt.callLater(() => ShellBridge.navigate("panel:properties.speed"))
                }
                onClosed: Qt.callLater(function() { quickMenuLoader.active = false })

                ThemedMenuItem { text: qsTr("Normal 1×"); onTriggered: quickMenu.setSpeed(1.0) }
                ThemedMenuItem { sectionHeader: true; text: qsTr("Slow") }
                ThemedMenuItem { text: "0.5×"; onTriggered: quickMenu.setSpeed(0.5) }
                ThemedMenuItem { text: "0.25×"; onTriggered: quickMenu.setSpeed(0.25) }
                ThemedMenuItem { text: "0.1×"; onTriggered: quickMenu.setSpeed(0.1) }
                ThemedMenuItem { sectionHeader: true; text: qsTr("Fast") }
                ThemedMenuItem { text: "2×"; onTriggered: quickMenu.setSpeed(2.0) }
                ThemedMenuItem { text: "4×"; onTriggered: quickMenu.setSpeed(4.0) }
                ThemedMenuItem { text: "8×"; onTriggered: quickMenu.setSpeed(8.0) }
                ThemedMenuItem { text: "20×"; onTriggered: quickMenu.setSpeed(20.0) }
                ThemedMenuSeparator { }
                ThemedMenuItem {
                    text: qsTr("Uniform Speed…")
                    onTriggered: quickMenu.openPage("uniform")
                }
                ThemedMenuItem {
                    text: qsTr("Speed Ramping…")
                    visible: quickMenu.media
                    onTriggered: quickMenu.openPage("ramping")
                }
                ThemedMenuItem {
                    text: qsTr("Hide Speed Control")
                    visible: !!quickMenu.clipOwner.clipData.speedControl
                    onTriggered: EditorState.setSpeedControlVisible(quickMenu.clipOwner.trackIndex,
                                                                    quickMenu.clipOwner.clipIndex, false)
                }
            }
        }
        onLoaded: item.popup()
    }
}
