import QtQuick
import Drift

// Wraps a built-in asset tab (Stickers, Audio effects, Text styles) with an "Online | Built-in"
// switch at the top. Online shows OnlineAssetBrowser for `onlineKinds`; the choice is remembered
// per tab. `builtin` is the tab's original component, untouched.
//
// Dafin: files dragged onto the tab from the computer join the user's own library for it —
// Lottie files go to My Lotties (Stickers), audio to My sounds (Audio), .mogrt templates (or a
// .zip with one and its preview) to Titles — and the view jumps to them. The assets panel's own
// DropArea routes the drop (AssetsPanel.routeDroppedUrls) and calls setOnline/showUserFiles here;
// other files are imported into Media as before.
Item {
    id: root

    required property string tabId
    property var onlineKinds: []
    property string builtinLabel: qsTr("Built-in")
    property Component builtin
    property bool allowLinks: true

    readonly property string userCollection: OnlineAssets.userCollectionForKinds(root.onlineKinds)

    function showUserFiles() {
        if (!root.online)
            root.setOnline(true)
        if (content.item && content.item.showUserFiles)
            content.item.showUserFiles()
    }

    // AssetsPanel remembers search/category per tab through these (forwarded to the active side).
    property string searchText: ""
    property string activeCategory: ""
    readonly property var categories: content.item && content.item.categories ? content.item.categories : []

    signal added()
    signal builtinLoaded(var item)
    // Dafin: the built-in side while it is shown (Titles' TextAssetsTab, say), else null.
    readonly property Item builtinItem: !root.online ? content.item : null

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
        anchors.bottom: dropHint.visible ? dropHint.top : parent.bottom
        anchors.bottomMargin: dropHint.visible ? Theme.spacingXs : 0
        sourceComponent: root.online ? onlineComponent : root.builtin
        onLoaded: {
            if (!root.online)
                root.builtinLoaded(item)
        }
    }

    // A quiet reminder, in both modes, that the tab takes the user's own files.
    Text {
        id: dropHint
        visible: root.userCollection.length > 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.spacingSm
        anchors.rightMargin: Theme.spacingSm
        anchors.bottomMargin: Theme.spacingXs
        wrapMode: Text.WordWrap
        maximumLineCount: 2
        elide: Text.ElideRight
        color: Theme.mutedForeground
        opacity: 0.85
        font.pixelSize: Theme.fontSizeXs
        text: root.userCollection === "titles"
              ? qsTr("You can add new title effects by dragging .mogrt files (or a .zip with a .mogrt and its preview) into this tab and pressing Save.")
              : root.userCollection === "sounds"
                ? qsTr("You can add new sounds by dragging audio files into this tab and pressing Save. They stay in My sounds.")
                : qsTr("You can add new Lotties by dragging .json or .lottie files into this tab and pressing Save. They stay in My Lotties.")
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
