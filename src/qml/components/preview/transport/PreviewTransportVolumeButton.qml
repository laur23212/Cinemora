import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift

// Dafin: the preview's speaker button, as in Filmora: a slider for how loud the preview plays (0 to
// 200 %) and a mute toggle. It is the monitor level only; the volume of a clip is in Properties >
// Audio and the one that reaches the export. Double-click the percentage for 100 %.
IconButton {
    id: button

    readonly property real level: EditorState.masterMuted ? 0 : EditorState.masterVolume

    Layout.alignment: Qt.AlignVCenter
    glyph: button.level <= 0.001 ? Theme.icons.volumeOff : Theme.icons.volumeHigh
    variant: "text"
    tooltip: qsTr("Preview volume (%1 %)").arg(Math.round(button.level * 100))
    active: volumePopup.visible
    onClicked: volumePopup.opened ? volumePopup.close() : volumePopup.open()

    Popup {
        id: volumePopup
        x: (button.width - width) / 2
        y: -height - Theme.spacingMd
        padding: Theme.spacingLg
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

        background: Rectangle {
            color: Theme.c.previewOptionsMenu.background
            border.width: Theme.c.previewOptionsMenu.borderWidth
            border.color: Theme.c.previewOptionsMenu.border
            radius: Theme.c.previewOptionsMenu.radius
        }

        contentItem: RowLayout {
            spacing: Theme.spacingMd

            IconButton {
                glyph: EditorState.masterMuted ? Theme.icons.volumeOff : Theme.icons.volumeHigh
                variant: "text"
                active: EditorState.masterMuted
                tooltip: EditorState.masterMuted ? qsTr("Unmute") : qsTr("Mute")
                onClicked: EditorState.masterMuted = !EditorState.masterMuted
            }
            ThemedSlider {
                id: slider
                label: qsTr("Preview volume")
                Layout.preferredWidth: 170
                from: 0
                to: 2
                stepSize: 0.01
                valueFormatter: v => Math.round(v * 100) + " %"
                onMoved: {
                    if (EditorState.masterMuted) EditorState.masterMuted = false
                    EditorState.masterVolume = value
                }
                Binding on value {
                    when: !slider.pressed
                    value: Math.min(2, EditorState.masterVolume)
                }
            }
            ThemedLabel {
                Layout.preferredWidth: 48
                horizontalAlignment: Text.AlignRight
                font.family: Theme.monoFontFamily
                size: "xs"
                text: Math.round(button.level * 100) + " %"
                MouseArea {
                    anchors.fill: parent
                    onDoubleClicked: { EditorState.masterMuted = false; EditorState.masterVolume = 1.0 }
                }
            }
        }
    }
}
