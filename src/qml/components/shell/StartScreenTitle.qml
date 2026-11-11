import QtQuick
import Drift

Column {
    width: parent.width
    spacing: Theme.spacingSm

    Text {
        text: "Dafin"
        color: Theme.c.startScreenTitle.foreground
        font.family: Theme.fontFamily
        font.pixelSize: 44
        font.weight: Font.Bold
    }

    Text {
        text: qsTr("Create polished videos fast")
        color: Theme.c.startScreenTitle.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: 20
    }
}
