import QtQuick
import QtQuick.Controls.Basic
import Drift

// Online assets for one panel (Stickers, Audio, Stock Video, Titles). Filmora layout: sources on
// the left, the provider notice + search above a grid of large previews on the right. Results
// come from AssetSearchModel (all usable providers for the panel's kinds, interleaved, loaded a
// page at a time as the grid scrolls).
Item {
    id: root

    // e.g. ["video"], ["music", "sfx"], ["sticker"], ["title"]
    property var kinds: ["video"]
    property bool allowLinks: true
    property string searchText: ""
    // "<kind>" = everything of that kind, "<kind>|<providerId>" = one provider.
    property string activeCategory: ""
    readonly property var categories: sidebarModel

    signal added()

    readonly property string activeKind: activeCategory.length > 0 ? activeCategory.split("|")[0] : kinds[0]
    readonly property string activeProvider: activeCategory.indexOf("|") > 0 ? activeCategory.split("|")[1] : ""

    readonly property var kindLabels: ({
        "video": qsTr("Stock video"), "music": qsTr("Music"), "sfx": qsTr("Sound effects"),
        "sticker": qsTr("Stickers"), "title": qsTr("Titles")
    })

    // Sidebar entries: for each kind, "All <kind>" then one row per provider.
    readonly property var sidebarModel: {
        void OnlineAssets.providers
        const out = []
        for (const k of root.kinds) {
            out.push({ id: k, name: qsTr("All %1").arg((root.kindLabels[k] || k).toLowerCase()), heading: root.kindLabels[k] || k,
                       locked: false, provider: "" })
            for (const p of OnlineAssets.providersFor(k)) {
                out.push({ id: k + "|" + p.id, name: p.name, heading: "", provider: p.id,
                           locked: p.requiresAccount && p.state !== "connected", state: p.state })
            }
        }
        return out
    }

    AssetSearchModel {
        id: results
        kinds: [root.activeKind]
        providerId: root.activeProvider
        query: root.searchText.trim()
        pageSize: root.activeKind === "sticker" ? 40 : 30
    }

    Component.onCompleted: {
        if (!activeCategory)
            activeCategory = kinds[0]
    }

    // ─── left: sources ──────────────────────────────────────────────────────────────────────
    Rectangle {
        id: sidebar
        width: Math.min(176, Math.max(130, root.width * 0.3))
        height: parent.height
        color: Theme.panelBackground

        ListView {
            anchors.fill: parent
            anchors.topMargin: Theme.spacingSm
            anchors.bottomMargin: manageLink.height + Theme.spacingSm
            clip: true
            model: root.sidebarModel
            boundsBehavior: Flickable.StopAtBounds
            delegate: Column {
                id: entry
                required property var modelData
                width: ListView.view.width

                Text {
                    visible: entry.modelData.heading.length > 0 && root.kinds.length > 1
                    leftPadding: Theme.spacingMd
                    topPadding: Theme.spacingSm
                    bottomPadding: 2
                    text: entry.modelData.heading
                    color: Theme.mutedForeground
                    font.pixelSize: Theme.fontSizeXs
                    font.capitalization: Font.AllUppercase
                }
                Rectangle {
                    readonly property bool selected: root.activeCategory === entry.modelData.id
                    x: Theme.spacingSm
                    width: entry.width - Theme.spacingSm * 2
                    height: 30
                    radius: Theme.radiusSm
                    color: selected ? Theme.panelAccent : (rowHover.hovered ? Theme.c.assetsPanel.optionBackgroundHover : "transparent")

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        x: entry.modelData.provider ? Theme.spacingLg : Theme.spacingMd
                        width: parent.width - x - 22
                        elide: Text.ElideRight
                        text: entry.modelData.name
                        color: parent.selected ? Theme.primary : entry.modelData.locked ? Theme.mutedForeground : Theme.panelForeground
                        font.pixelSize: Theme.fontSizeBase
                        font.weight: entry.modelData.provider ? Font.Normal : Font.DemiBold
                    }
                    IconGlyph {
                        visible: entry.modelData.locked || entry.modelData.state === "expired"
                        anchors.right: parent.right
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: Theme.icons.lock
                        iconSize: 12
                        iconColor: entry.modelData.state === "expired" ? "#e67e22" : Theme.mutedForeground
                    }
                    HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: {
                            if (entry.modelData.locked)
                                ShellBridge.main.openSettings("assetproviders")
                            else
                                root.activeCategory = entry.modelData.id
                        }
                    }
                    ThemedToolTip {
                        visible: rowHover.hovered && entry.modelData.locked
                        text: qsTr("Connect %1 in Settings to browse it here.").arg(entry.modelData.name)
                    }
                }
            }
        }

        Text {
            id: manageLink
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: Theme.spacingMd
            textFormat: Text.StyledText
            linkColor: Theme.primary
            font.pixelSize: Theme.fontSizeSm
            text: "<a href=\"#\">" + qsTr("Manage providers") + "</a>"
            onLinkActivated: ShellBridge.main.openSettings("assetproviders")
            HoverHandler { cursorShape: Qt.PointingHandCursor }
        }

        Rectangle {
            anchors.right: parent.right
            width: 1
            height: parent.height
            color: Theme.panelBorder
        }
    }

    // ─── right: notice, search, grid ────────────────────────────────────────────────────────
    Item {
        id: main
        anchors.left: sidebar.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacingSm

        Column {
            id: header
            width: parent.width
            spacing: Theme.spacingXs

            AssetProvidersNotice { width: parent.width }

            AssetSearchBar {
                width: parent.width
                placeholderText: qsTr("Search %1").arg((root.kindLabels[root.activeKind] || "").toLowerCase())
                text: root.searchText
                onTextChanged: root.searchText = text
            }

            Row {
                visible: root.allowLinks
                width: parent.width
                spacing: Theme.spacingXs
                ThemedTextField {
                    id: linkField
                    width: parent.width - addLink.width - parent.spacing
                    placeholderText: root.activeKind === "sticker" ? qsTr("Paste a Lottie JSON link…")
                                                                   : qsTr("Paste a direct file link…")
                    onAccepted: addLink.clicked()
                }
                ThemedButton {
                    id: addLink
                    variant: "secondary"
                    glyph: Theme.icons.download
                    tooltip: qsTr("Download the link and add it to Media")
                    enabled: linkField.text.trim().length > 0
                    onClicked: {
                        if (OnlineAssets.importLink(linkField.text))
                            linkField.text = ""
                    }
                }
            }

            // Attribution the providers' terms ask for, for the sources shown right now.
            Flow {
                width: parent.width
                spacing: Theme.spacingSm
                Repeater {
                    model: results.sources.filter(s => !s.locked && s.attributionText
                                                       && (root.activeProvider === "" || root.activeProvider === s.id))
                    delegate: Text {
                        required property var modelData
                        textFormat: Text.StyledText
                        linkColor: Theme.mutedForeground
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        text: "<a href=\"" + modelData.attributionUrl + "\">" + modelData.attributionText + "</a>"
                        onLinkActivated: (link) => OnlineAssets.openExternal(link)
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                    }
                }
            }

            // Problems from this round (rate limits, outages, expired sign-ins), one line each.
            Repeater {
                model: results.problems.filter(p => p.code !== "authRequired")
                delegate: Rectangle {
                    required property var modelData
                    width: header.width
                    height: problemText.implicitHeight + 8
                    radius: Theme.radiusSm
                    color: modelData.code === "authExpired" ? "#33e67e22" : "#22c0392b"
                    Text {
                        id: problemText
                        anchors.left: parent.left
                        anchors.right: retry.left
                        anchors.margins: 6
                        anchors.verticalCenter: parent.verticalCenter
                        wrapMode: Text.WordWrap
                        color: Theme.panelForeground
                        font.pixelSize: Theme.fontSizeXs
                        text: parent.modelData.message
                    }
                    ThemedButton {
                        id: retry
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        variant: "ghost"
                        text: parent.modelData.code === "authExpired" ? qsTr("Reconnect") : qsTr("Retry")
                        onClicked: {
                            if (parent.modelData.code === "authExpired")
                                ShellBridge.main.openSettings("assetproviders")
                            else
                                results.refresh()
                        }
                    }
                }
            }
        }

        GridView {
            id: grid
            anchors.top: header.bottom
            anchors.topMargin: Theme.spacingSm
            anchors.bottom: parent.bottom
            width: parent.width
            clip: true
            model: results
            boundsBehavior: Flickable.StopAtBounds
            cacheBuffer: 300
            ScrollBar.vertical: AppScrollBar {}

            readonly property bool audio: root.activeKind === "music" || root.activeKind === "sfx"
            readonly property real minCell: root.activeKind === "sticker" ? 104 : audio ? 230 : 156
            readonly property int columns: Math.max(1, Math.floor(width / minCell))
            readonly property real aspect: root.activeKind === "sticker" ? 1.0 : audio ? 0.42 : 9 / 16
            cellWidth: Math.floor(width / columns)
            cellHeight: Math.round((cellWidth - Theme.spacingSm) * aspect) + 44

            delegate: Item {
                required property var item
                required property string thumbnail
                width: grid.cellWidth
                height: grid.cellHeight
                OnlineAssetCard {
                    item: parent.item
                    thumbnail: parent.thumbnail
                    cardWidth: grid.cellWidth - Theme.spacingSm
                    onAdded: root.added()
                }
            }

            onContentYChanged: {
                if (contentHeight - contentY - height < cellHeight * 2)
                    results.loadMore()
            }
            onHeightChanged: if (contentHeight < height) results.loadMore()

            footer: Item {
                width: grid.width
                height: results.loading ? 44 : 0
                CircularProgress {
                    anchors.centerIn: parent
                    visible: results.loading
                    indeterminate: true
                    size: 22
                }
            }
        }

        // Empty states
        Column {
            anchors.centerIn: grid
            width: grid.width * 0.85
            spacing: Theme.spacingSm
            visible: !results.loading && results.count === 0

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.mutedForeground
                font.pixelSize: Theme.fontSizeBase
                text: {
                    const locked = results.problems.filter(p => p.code === "authRequired")
                    const failed = results.problems.filter(p => p.code !== "authRequired")
                    if (failed.some(p => p.code === "offline"))
                        return qsTr("You're offline. Check your internet connection and try again.")
                    if (failed.length > 0)
                        return qsTr("The sources couldn't answer right now.")
                    if (locked.length > 0 && results.sources.every(s => s.locked))
                        return qsTr("Connect a free account to browse here: %1.").arg(locked.map(p => p.name).join(", "))
                    return root.searchText.trim().length > 0 ? qsTr("Nothing found for \"%1\".").arg(root.searchText.trim())
                                                             : qsTr("Nothing here yet.")
                }
            }
            ThemedButton {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: results.problems.length > 0
                variant: "secondary"
                text: results.problems.some(p => p.code === "authRequired") && results.sources.every(s => s.locked)
                      ? qsTr("Manage Asset Providers") : qsTr("Try again")
                onClicked: {
                    if (text === qsTr("Try again"))
                        results.refresh()
                    else
                        ShellBridge.main.openSettings("assetproviders")
                }
            }
        }
    }

    ToolTip {
        id: toast
        x: (root.width - width) / 2
        y: root.height - height - Theme.spacingLg
        timeout: 5000
    }
    Connections {
        target: OnlineAssets
        function onDownloadFailed(key, message) {
            toast.text = message
            toast.open()
        }
        function onNotice(message) {
            toast.text = message
            toast.open()
        }
    }
}
