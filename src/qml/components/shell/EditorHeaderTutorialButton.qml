import QtQuick
import Drift

// Dafin: the header's Tutorial button, left of the Marketplace button. Opens the feature list.
Item {
    id: tutorialButton
    implicitWidth: button.implicitWidth
    implicitHeight: button.implicitHeight
    width: implicitWidth
    height: implicitHeight
    anchors.verticalCenter: parent ? parent.verticalCenter : undefined

    IconButton {
        id: button
        anchors.fill: parent
        glyph: Theme.icons.bookOpen
        variant: "ghost"
        tooltip: qsTr("Tutorial — what each part of Dafin does, with guided tours")
        onClicked: ShellBridge.main.openTutorial("")
    }
}
