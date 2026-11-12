import QtQuick
import Drift

// Small "?" in a panel's top-right corner. Opens that panel's page in the Tutorial; the page has
// a Start guided tour button. Hidden on touch layouts, which have no tutorial.
Item {
    id: root

    // Tutorial feature id, e.g. "timeline-panel".
    property string featureId: ""

    // False for panels pinned to one tab or shown in a phone sheet.
    property bool allowed: true

    readonly property bool shown: allowed && !Theme.compact && Qt.platform.os !== "android"
    visible: shown
    width: 18
    height: 18
    z: 1000

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: hover.hovered ? Theme.c.iconButton.foregroundActive : Theme.c.panelFrame.border
        opacity: hover.hovered ? 0.9 : 0.55
    }

    Text {
        anchors.centerIn: parent
        text: "?"
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeSm
        font.weight: Font.DemiBold
        color: hover.hovered ? Theme.c.panelFrame.background : Theme.c.iconButton.foreground
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        onTapped: {
            if (ShellBridge.main && ShellBridge.main.openTutorial)
                ShellBridge.main.openTutorial(root.featureId)
        }
    }

    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Help for this panel")
    Accessible.onPressAction: {
        if (ShellBridge.main && ShellBridge.main.openTutorial)
            ShellBridge.main.openTutorial(root.featureId)
    }
}
