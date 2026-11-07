import QtQuick
import Drift

// Banner at the top of every online asset panel:
// "More assets are available when you connect free asset provider accounts. Manage Asset Providers"
// The link opens Settings > Asset Providers directly.
Rectangle {
    id: root

    property string message: qsTr("More assets are available when you connect free asset provider accounts.")

    implicitHeight: label.implicitHeight + Theme.spacingSm * 2
    radius: Theme.radiusSm
    color: Theme.c.assetsPanel.pickerBackground
    border.width: 1
    border.color: Theme.panelBorder

    IconGlyph {
        id: icon
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingSm
        anchors.verticalCenter: parent.verticalCenter
        glyph: Theme.icons.info
        iconSize: 14
        iconColor: Theme.primary
    }

    Text {
        id: label
        anchors.left: icon.right
        anchors.leftMargin: Theme.spacingSm
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingSm
        anchors.verticalCenter: parent.verticalCenter
        wrapMode: Text.WordWrap
        textFormat: Text.StyledText
        linkColor: Theme.primary
        color: Theme.panelForeground
        font.pixelSize: Theme.fontSizeSm
        text: root.message + " <a href=\"settings\">" + qsTr("Manage Asset Providers") + "</a>"
        onLinkActivated: ShellBridge.main.openSettings("assetproviders")

        HoverHandler {
            cursorShape: label.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
        }
    }

    Accessible.role: Accessible.StaticText
    Accessible.name: root.message
}
