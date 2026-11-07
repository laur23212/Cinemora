import QtQuick
import Drift

// Wraps a built-in asset tab (Stickers, Audio effects, Text styles) with an "Online | Built-in"
// switch at the top. Online shows OnlineAssetBrowser for `onlineKinds`; the choice is remembered
// per tab. `builtin` is the tab's original component, untouched.
Item {
    id: root

    required property string tabId
    property var onlineKinds: []
    property string builtinLabel: qsTr("Built-in")
    property Component builtin
    property bool allowLinks: true

    // AssetsPanel remembers search/category per tab through these (forwarded to the active side).
    property string searchText: ""
    property string activeCategory: ""
    readonly property var categories: content.item && content.item.categories ? content.item.categories : []

    signal added()
    signal builtinLoaded(var item)

    property bool online: LayoutMemory.layoutString("assets/" + tabId + "/mode", "online") === "online"
    function setOnline(on) {
        root.online = on
        LayoutMemory.setLayoutString("assets/" + root.tabId + "/mode", on ? "online" : "builtin")
    }

    Row {
        id: switcher
        x: Theme.spacingSm
        y: Theme.spacingXs
        height: 30
        spacing: 2

        Repeater {
            model: [{ online: true, label: qsTr("Online") }, { online: false, label: root.builtinLabel }]
            delegate: Rectangle {
                required property var modelData
                readonly property bool selected: root.online === modelData.online
                width: segLabel.implicitWidth + Theme.spacingLg * 2
                height: switcher.height
                radius: Theme.radiusSm
                color: selected ? Theme.panelAccent : (segHover.hovered ? Theme.c.assetsPanel.optionBackgroundHover : "transparent")
                border.width: selected ? 1 : 0
                border.color: Theme.panelBorder
                Text {
                    id: segLabel
                    anchors.centerIn: parent
                    text: parent.modelData.label
                    color: parent.selected ? Theme.primary : Theme.panelForeground
                    font.pixelSize: Theme.fontSizeSm
                    font.weight: parent.selected ? Font.DemiBold : Font.Normal
                }
                HoverHandler { id: segHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.setOnline(parent.modelData.online) }
                Accessible.role: Accessible.RadioButton
                Accessible.name: modelData.label
                Accessible.checked: selected
            }
        }
    }

    Loader {
        id: content
        anchors.top: switcher.bottom
        anchors.topMargin: Theme.spacingXs
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        sourceComponent: root.online ? onlineComponent : root.builtin
        onLoaded: {
            if (!root.online)
                root.builtinLoaded(item)
        }
    }

    Component {
        id: onlineComponent
        OnlineAssetBrowser {
            kinds: root.onlineKinds
            allowLinks: root.allowLinks
            onAdded: root.added()
        }
    }
}
