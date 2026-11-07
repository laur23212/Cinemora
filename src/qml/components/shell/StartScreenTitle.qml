import QtQuick
import Drift

Column {
    width: parent.width
    spacing: Theme.spacingXs

    Text {
        text: "Cadra"
        color: Theme.c.startScreenTitle.foreground
        font.family: Theme.fontFamily
        font.pixelSize: 26
        font.weight: Font.Bold
    }

    Text {
        text: qsTr("Create polished videos fast")
        color: Theme.c.startScreenTitle.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeBase
    }
}
