import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin: the words of a Lottie title, edited from the preview. A click on a selected Lottie clip
// that has text slots opens this card over the bottom of the picture with one field per slot,
// filled with what the title shows now; Enter (or Done) writes them as the clip's slot values,
// one undo step each, and Esc or a click elsewhere leaves them as they were. A Lottie with no
// text slots opens nothing.
Item {
    id: root

    property bool active: false
    property int editTrack: -1
    property int editClip: -1
    property var fields: []

    anchors.fill: parent
    visible: active

    function start(trackIndex, clipIndex) {
        const slots = EditorState.vectorTextSlots(trackIndex, clipIndex)
        if (!slots || slots.length === 0)
            return
        EditorState.playing = false
        root.editTrack = trackIndex
        root.editClip = clipIndex
        root.fields = slots
        root.active = true
        Qt.callLater(function () {
            const first = fieldRepeater.itemAt(0)
            if (first) {
                first.forceActiveFocus()
                first.selectAll()
            }
        })
    }

    function commit() {
        if (!root.active)
            return
        for (let i = 0; i < fieldRepeater.count; ++i) {
            const field = fieldRepeater.itemAt(i)
            const slot = root.fields[i]
            if (field && slot && field.text !== slot.text)
                EditorState.setVectorSlot(root.editTrack, root.editClip, slot.id, field.text)
        }
        root.close()
    }

    function close() {
        root.active = false
        root.fields = []
    }

    // The selection moving on (or the clip going away) ends the edit, keeping what was typed.
    Connections {
        target: EditorState
        enabled: root.active
        function onSelectionChanged() {
            if (EditorState.selectedTrack !== root.editTrack || EditorState.selectedClip !== root.editClip)
                root.commit()
        }
    }

    // A click on the picture outside the card finishes the edit.
    MouseArea {
        anchors.fill: parent
        onPressed: (mouse) => {
            mouse.accepted = false
            root.commit()
        }
    }

    Rectangle {
        id: card
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacingLg
        width: Math.min(parent.width - Theme.spacingLg * 2, 420)
        height: column.implicitHeight + Theme.spacingMd * 2
        radius: Theme.radiusMd
        color: Theme.c.propertiesPanel.background
        border.width: 1
        border.color: Theme.primary

        // Clicks on the card stay on the card.
        MouseArea { anchors.fill: parent }

        Column {
            id: column
            x: Theme.spacingMd
            y: Theme.spacingMd
            width: parent.width - Theme.spacingMd * 2
            spacing: Theme.spacingSm

            Text {
                width: parent.width
                text: qsTr("Title text — Enter to apply, Esc to cancel")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                elide: Text.ElideRight
            }

            Repeater {
                id: fieldRepeater
                model: root.fields
                delegate: ThemedTextField {
                    required property var modelData
                    width: column.width
                    text: modelData.text
                    font.family: Theme.fontFamily
                    placeholderText: modelData.id
                    Keys.onReturnPressed: root.commit()
                    Keys.onEnterPressed: root.commit()
                    Keys.onEscapePressed: root.close()
                }
            }

            Row {
                anchors.right: parent.right
                spacing: Theme.spacingSm
                ThemedButton {
                    text: qsTr("Cancel")
                    variant: "ghost"
                    onClicked: root.close()
                }
                ThemedButton {
                    text: qsTr("Done")
                    variant: "primary"
                    onClicked: root.commit()
                }
            }
        }
    }
}
