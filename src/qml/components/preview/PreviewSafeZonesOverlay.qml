import QtQuick
import Drift

// Dafin: the broadcast safe areas, drawn over the picture while "Show Safe Zones" is on. The outer
// frame is the action-safe area (90 % of the picture), the inner one the title-safe area (80 %).
// Nothing here takes the pointer.
Item {
    id: root
    enabled: false

    Rectangle {
        anchors.centerIn: parent
        width: parent.width * 0.9
        height: parent.height * 0.9
        color: "transparent"
        border.width: 1
        border.color: "#E6FFFFFF"
        Text {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 3
            text: qsTr("Action safe")
            color: "#E6FFFFFF"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            style: Text.Outline
            styleColor: "#99000000"
        }
    }
    Rectangle {
        anchors.centerIn: parent
        width: parent.width * 0.8
        height: parent.height * 0.8
        color: "transparent"
        border.width: 1
        border.color: "#E6FFD84D"
        Text {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 3
            text: qsTr("Title safe")
            color: "#E6FFD84D"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            style: Text.Outline
            styleColor: "#99000000"
        }
    }
}
