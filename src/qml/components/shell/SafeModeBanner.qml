import QtQuick
import Drift

// Shown for the whole run when Dafin started in safe mode (two start-ups in a row failed, or
// --safe-mode). Restart normally comes back with everything on.
Rectangle {
    id: banner

    signal restartRequested()

    height: row.implicitHeight + 16
    color: "#b45309"

    Row {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 12
        spacing: 12

        Text {
            id: message
            width: parent.width - restart.width - parent.spacing
            anchors.verticalCenter: parent.verticalCenter
            wrapMode: Text.WordWrap
            color: "white"
            font.pixelSize: 13
            text: qsTr("Safe mode: Dafin did not start properly twice, so GPU acceleration, local AI, add-ons and "
                       + "your templates are off and the preview is at its lowest quality. Your files and "
                       + "settings are untouched.")
        }

        ThemedButton {
            id: restart
            anchors.verticalCenter: parent.verticalCenter
            variant: "secondary"
            text: qsTr("Restart normally")
            onClicked: banner.restartRequested()
        }
    }
}
