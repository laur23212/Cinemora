import QtQuick
import QtQuick.Controls.Basic
import Drift

// Online assets for one panel (Stickers, Audio, Stock Video, Titles). Filmora layout: sources on
// the left, the provider notice + search above a grid of large previews on the right. Results
// come from AssetSearchModel (all usable providers for the panel's kinds, interleaved, loaded a
// page at a time as the grid scrolls).
//
// Dafin: the user's own files lead the sidebar ("My Lotties" in Stickers, "My sounds" in Audio);
// imported .mogrt titles have no row and open the Titles grid. Files are dropped on the panel
// (OnlineSwitchTab) or picked with "Add files…".
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

    // ─── the user's own files ───────────────────────────────────────────────────────────────
    readonly property string userCollection: OnlineAssets.userCollectionForKinds(root.kinds)
    readonly property string userProvider: OnlineAssets.userCollectionProvider(root.userCollection)
    readonly property string userCollectionName: OnlineAssets.userCollectionName(root.userCollection)
    // Where those files are listed: their own row, or for titles the main grid.
    readonly property string userCategory: {
        if (root.userCollection === "titles")
            return "title"
        for (const entry of root.sidebarModel) {
            if (entry.provider === root.userProvider)
                return entry.id
        }
        return ""
    }
    readonly property bool showingUserFiles: root.userCollection.length > 0 && root.activeCategory === root.userCategory
    readonly property int unsavedCount: (OnlineAssets.userLibraryRevision,
                                         root.userCollection.length > 0 ? OnlineAssets.unsavedUserItems(root.userCollection) : 0)
    readonly property int userItemCount: (OnlineAssets.userLibraryRevision,
                                          root.userCollection.length > 0 ? OnlineAssets.userItemCount(root.userCollection) : 0)

    function showUserFiles() {
        if (root.userCategory.length > 0) {
            root.searchText = ""
            root.activeCategory = root.userCategory
        }
    }
    function browseUserFiles() {
        const urls = FileDialogs.openFiles(root.userCollection === "titles" ? qsTr("Add Motion Graphics Templates")
                                                                           : qsTr("Add to %1").arg(root.userCollectionName),
                                           [OnlineAssets.userCollectionFilter(root.userCollection), qsTr("All Files (*)")])
        if (!urls || urls.length === 0)
            return
        const result = OnlineAssets.importUserFiles(root.userCollection, urls)
        if (result.added > 0 || result.duplicates > 0)
            root.showUserFiles()
    }

    // One .dafinlib file holds every saved Lottie, sound and title, for Dafin on another computer.
    function exportUserLibrary() {
        const day = new Date().toISOString().slice(0, 10)
        const url = FileDialogs.saveFile(qsTr("Export my library"), [OnlineAssets.userLibraryArchiveFilter()],
                                         qsTr("Dafin library %1").arg(day), ".dafinlib")
        if (url && url.toString().length > 0)
            OnlineAssets.exportUserLibrary(url)
    }
    function importUserLibrary() {
        const url = FileDialogs.openFile(qsTr("Import a Dafin library"),
                                         [OnlineAssets.userLibraryArchiveFilter(), qsTr("All Files (*)")])
        if (!url || url.toString().length === 0)
            return
        const result = OnlineAssets.importUserLibrary(url)
        if (result.count > 0)
            root.showUserFiles()
    }

    // Sidebar entries: the user's own collection first, then for each kind "All <kind>" and one
    // row per provider.
    readonly property var sidebarModel: {
        void OnlineAssets.providers
        const out = []
        for (const k of root.kinds) {
            for (const p of OnlineAssets.providersFor(k)) {
                if (p.userLibrary && p.ownSidebarEntry)
                    out.push({ id: k + "|" + p.id, name: p.name, heading: "", provider: p.id,
                               locked: false, state: p.state, user: true })
            }
        }
        if (out.length > 0)
            out.push({ id: "", name: "", heading: "", provider: "", locked: false, separator: true })
        for (const k of root.kinds) {
            out.push({ id: k, name: qsTr("All %1").arg((root.kindLabels[k] || k).toLowerCase()), heading: root.kindLabels[k] || k,
                       locked: false, provider: "" })
            for (const p of OnlineAssets.providersFor(k)) {
                if (p.userLibrary)
                    continue
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

                // Between the user's own collection and the online sources.
                Item {
                    visible: !!entry.modelData.separator
                    width: entry.width
                    height: visible ? Theme.spacingSm * 2 + 1 : 0
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        x: Theme.spacingMd
                        width: parent.width - Theme.spacingMd * 2
                        height: 1
                        color: Theme.panelBorder
                    }
                }
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
                    visible: !entry.modelData.separator
                    x: Theme.spacingSm
                    width: entry.width - Theme.spacingSm * 2
                    height: visible ? 30 : 0
                    radius: Theme.radiusSm
                    color: selected ? Theme.panelAccent : (rowHover.hovered ? Theme.c.assetsPanel.optionBackgroundHover : "transparent")

                    IconGlyph {
                        id: userGlyph
                        visible: !!entry.modelData.user
                        anchors.verticalCenter: parent.verticalCenter
                        x: Theme.spacingMd
                        glyph: Theme.icons.folder
                        iconSize: 13
                        iconColor: parent.selected ? Theme.primary : Theme.mutedForeground
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        x: entry.modelData.user ? Theme.spacingMd + userGlyph.width + 6
                                                : entry.modelData.provider ? Theme.spacingLg : Theme.spacingMd
                        width: parent.width - x - 22
                        elide: Text.ElideRight
                        text: entry.modelData.name
                        color: parent.selected ? Theme.primary : entry.modelData.locked ? Theme.mutedForeground : Theme.panelForeground
                        font.pixelSize: Theme.fontSizeBase
                        font.weight: entry.modelData.provider && !entry.modelData.user ? Font.Normal : Font.DemiBold
                    }
                    // Files waiting to be saved.
                    Rectangle {
                        visible: !!entry.modelData.user && root.unsavedCount > 0
                        anchors.right: parent.right
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(16, unsavedCountText.implicitWidth + 8)
                        height: 16
                        radius: 8
                        color: "#b9770e"
                        Text {
                            id: unsavedCountText
                            anchors.centerIn: parent
                            text: root.unsavedCount
                            color: "white"
                            font.pixelSize: Theme.fontSizeXs
                        }
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
                    tooltip: root.activeKind === "sticker" ? qsTr("Download the link, add it to Media and keep it in My Lotties")
                                                           : qsTr("Download the link and add it to Media")
                    enabled: linkField.text.trim().length > 0
                    onClicked: {
                        if (OnlineAssets.importLink(linkField.text))
                            linkField.text = ""
                    }
                }
            }

            // The user's own files: add more, keep the ones dropped this session.
            Flow {
                visible: root.showingUserFiles
                width: parent.width
                spacing: Theme.spacingXs
                ThemedButton {
                    variant: "secondary"
                    glyph: Theme.icons.folderInput
                    text: root.userCollection === "titles" ? qsTr("Add .mogrt…") : qsTr("Add files…")
                    tooltip: root.userCollection === "lotties" ? qsTr("Add Lottie animations (.json, .lottie) from your computer")
                           : root.userCollection === "sounds" ? qsTr("Add audio files from your computer")
                           : qsTr("Add Motion Graphics Templates (.mogrt) from your computer")
                    onClicked: root.browseUserFiles()
                }
                ThemedButton {
                    visible: root.unsavedCount > 0
                    variant: "primary"
                    glyph: Theme.icons.save
                    text: qsTr("Save all (%1)").arg(root.unsavedCount)
                    tooltip: qsTr("Keep every file added this session until you delete it")
                    onClicked: OnlineAssets.saveAllUserItems(root.userCollection)
                }
                // Moving everything saved (Lotties, sounds and titles together) to another computer.
                ThemedButton {
                    id: transferButton
                    variant: "secondary"
                    glyph: Theme.icons.package
                    tooltip: qsTr("Export your saved Lotties, sounds and titles to one file, or import one from another computer")
                    onClicked: transferMenu.popup(0, transferButton.height + Theme.spacingXs)

                    ThemedContextMenu {
                        id: transferMenu
                        implicitWidth: 240
                        ThemedMenuItem {
                            text: qsTr("Export my library…")
                            icon.name: Theme.icons.folderOutput
                            enabled: (OnlineAssets.userLibraryRevision, OnlineAssets.savedUserItemCount() > 0)
                            onTriggered: root.exportUserLibrary()
                        }
                        ThemedMenuItem {
                            text: qsTr("Import a library…")
                            icon.name: Theme.icons.folderInput
                            onTriggered: root.importUserLibrary()
                        }
                    }
                }
            }
            // How to add them is the tab's footer (OnlineSwitchTab); this is what happens to them.
            Text {
                visible: root.showingUserFiles && (root.userCollection !== "titles" ? root.unsavedCount > 0
                                                                                    : root.userItemCount > 0)
                width: parent.width
                wrapMode: Text.WordWrap
                color: Theme.mutedForeground
                font.pixelSize: Theme.fontSizeXs
                text: root.userCollection === "titles"
                      ? qsTr("A .mogrt is added as an editable Dafin title; its After Effects animation plays only in Adobe apps. Unsaved templates are removed when Dafin closes.")
                      : qsTr("Files you don't save are removed when Dafin closes.")
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
                    onDeleteRequested: (item) => deleteDialog.openFor(item)
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
                    if (root.searchText.trim().length > 0)
                        return qsTr("Nothing found for \"%1\".").arg(root.searchText.trim())
                    if (root.showingUserFiles && root.userCollection === "lotties")
                        return qsTr("No Lotties yet. Drag .json or .lottie animations here, or click Add files.")
                    if (root.showingUserFiles && root.userCollection === "sounds")
                        return qsTr("No sounds yet. Drag audio files here, or click Add files.")
                    return qsTr("Nothing here yet.")
                }
            }
            ThemedButton {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: root.showingUserFiles && root.userCollection !== "titles" && results.problems.length === 0
                         && root.searchText.trim().length === 0
                variant: "primary"
                glyph: Theme.icons.folderInput
                text: qsTr("Add files…")
                onClicked: root.browseUserFiles()
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

    // Deleting a saved file of the user's own (an unsaved one goes without asking).
    ThemedDialog {
        id: deleteDialog

        property var item: null

        title: qsTr("Delete from Dafin")
        acceptText: qsTr("Delete")
        acceptVariant: "destructive"
        preferredWidth: Theme.dialogWidthSm
        acceptOnReturn: false

        function openFor(item) {
            deleteDialog.item = item
            open()
        }

        onAccepted: {
            if (deleteDialog.item)
                OnlineAssets.deleteUserItem(deleteDialog.item)
            deleteDialog.item = null
        }

        contentItem: Text {
            width: parent ? parent.width : 320
            wrapMode: Text.WordWrap
            text: qsTr("Delete “%1” from %2? The file you added it from is not touched, and projects that already use it keep their own copy.")
                      .arg(deleteDialog.item ? deleteDialog.item.title : "")
                      .arg(root.userCollection === "titles" ? qsTr("Titles") : root.userCollectionName)
            color: Theme.panelForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
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
