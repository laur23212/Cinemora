import QtQuick
import Drift

// Dafin: the header's Shortcuts button, beside Agent. Opens the list of keyboard shortcuts, where
// each one can be changed (ShortcutsPopup).
Item {
    id: shortcutsButton
    implicitWidth: button.implicitWidth
    implicitHeight: button.implicitHeight
    width: implicitWidth
    height: implicitHeight
    anchors.verticalCenter: parent ? parent.verticalCenter : undefined

    IconButton {
        id: button
        anchors.fill: parent
        glyph: Theme.icons.keyboard
        variant: "ghost"
        text: qsTr("Shortcuts")
        active: popup.opened
        tooltip: qsTr("Keyboard shortcuts — see every one and change them")
        onClicked: popup.opened ? popup.close() : popup.open()
    }

    ShortcutsPopup {
        id: popup
        x: shortcutsButton.width - width
        y: shortcutsButton.height + Theme.spacingMd
    }
}
