import QtQuick
import QtQuick.Controls.Basic
import Drift

// "Stock Media" — free stock video, sound effects and Lottie animations from the sources in the
// user's catalog JSON (FreeLibrary). Left: the sources, grouped like Filmora's category list.
// Right: search, a box to paste a direct link, and the result grid with hover previews and a
// download button on every card. Everything downloaded here is deleted when Drift closes,
// except what a saved project uses (that is stored inside the .drift file).
Item {
    id: root

    property string searchText: ""
    property string activeCategory: ""     // source id; named like the other browser tabs
    readonly property var categories: FreeLibrary.sources

    signal added()

    readonly property var activeSource: {
        for (let i = 0; i < categories.length; ++i)
            if (categories[i].id === activeCategory)
                return categories[i]
        return categories.length > 0 ? categories[0] : ({})
    }
    // Also shown when the saved key was refused, so it can be replaced right there.
    readonly property bool needsKey: !!activeSource.needsKey && (!activeSource.hasKey || error === "bad-key")

    property int page: 1
    property int pendingToken: -1
    property bool loading: false
    property bool hasMore: false
    property string error: ""
    ListModel { id: results }

    readonly property var groups: [
        { kind: "video", label: qsTr("Videos") },
        { kind: "audio", label: qsTr("Sound effects") },
        { kind: "lottie", label: qsTr("Lottie animations") },
        { kind: "image", label: qsTr("Images") }
    ]

    function refresh() {
        results.clear()
        page = 1
        hasMore = false
        error = ""
        if (!activeSource.id)
            return
        loading = true
        pendingToken = FreeLibrary.search(activeSource.id, searchText, 1)
    }
    function loadMore() {
        if (loading || !hasMore || !activeSource.id)
            return
        loading = true
        page += 1
        pendingToken = FreeLibrary.search(activeSource.id, searchText, page)
    }

    onActiveCategoryChanged: refresh()
    Component.onCompleted: {
        if (!activeCategory && categories.length > 0)
            activeCategory = categories[0].id
        refresh()
    }

    Timer {
        id: searchDebounce
        interval: 400
        onTriggered: root.refresh()
    }
    onSearchTextChanged: searchDebounce.restart()

    Connections {
        target: FreeLibrary
        function onResults(token, sourceId, items, more, err) {
            if (token !== root.pendingToken)
                return
            root.loading = false
            root.hasMore = more
            root.error = err
            for (let i = 0; i < items.length; ++i)
                results.append({ entry: items[i] })
        }
        function onCatalogChanged() {
            if (!root.categories.some(c => c.id === root.activeCategory) && root.categories.length > 0)
                root.activeCategory = root.categories[0].id
            root.refresh()
        }
        function onFailed(key, message) {
            toast.text = message
            toast.open()
        }
    }

    // ─── Left: sources ──────────────────────────────────────────────────────────────────────
    Rectangle {
        id: sidebar
        width: Math.min(170, root.width * 0.34)
        height: parent.height
        color: Theme.panelBackground

        Flickable {
            anchors.fill: parent
            anchors.topMargin: Theme.spacingSm
            contentHeight: sideColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: sideColumn
                width: parent.width

                Repeater {
                    model: root.groups
                    delegate: Column {
                        id: group
                        required property var modelData
                        readonly property var members: root.categories.filter(c => c.kind === modelData.kind)
                        width: sideColumn.width
                        visible: members.length > 0

                        Text {
                            leftPadding: Theme.spacingMd
                            topPadding: Theme.spacingSm
                            bottomPadding: Theme.spacingXs
                            text: group.modelData.label
                            color: Theme.mutedForeground
                            font.pixelSize: Theme.fontSizeXs
                            font.capitalization: Font.AllUppercase
                        }

                        Repeater {
                            model: group.members
                            delegate: Rectangle {
                                id: srcRow
                                required property var modelData
                                readonly property bool selected: modelData.id === root.activeSource.id
                                width: group.width - Theme.spacingSm * 2
                                x: Theme.spacingSm
                                height: 32
                                radius: Theme.radiusSm
                                color: selected ? Theme.panelAccent : (rowHover.hovered ? Theme.c.assetsPanel.optionBackgroundHover : "transparent")

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: Theme.spacingMd
                                    width: parent.width - x - 20
                                    elide: Text.ElideRight
                                    text: srcRow.modelData.name
                                    color: srcRow.selected ? Theme.primary : Theme.panelForeground
                                    font.pixelSize: Theme.fontSizeBase
                                }
                                IconGlyph {
                                    visible: !!srcRow.modelData.needsKey && !srcRow.modelData.hasKey
                                    anchors.right: parent.right
                                    anchors.rightMargin: 6
                                    anchors.verticalCenter: parent.verticalCenter
                                    glyph: Theme.icons.lock
                                    iconSize: 12
                                    iconColor: Theme.mutedForeground
                                }
                                HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
                                TapHandler { onTapped: root.activeCategory = srcRow.modelData.id }
                            }
                        }
                    }
                }

                Item { width: 1; height: Theme.spacingMd }

                ThemedButton {
                    x: Theme.spacingSm
                    width: sideColumn.width - Theme.spacingSm * 2
                    variant: "ghost"
                    glyph: Theme.icons.pencil
                    text: qsTr("Edit catalog")
                    tooltip: qsTr("Add your own sources and Lottie links (opens the JSON file)")
                    onClicked: FreeLibrary.openCatalogFile()
                }
                ThemedButton {
                    x: Theme.spacingSm
                    width: sideColumn.width - Theme.spacingSm * 2
                    variant: "ghost"
                    glyph: Theme.icons.refresh
                    text: qsTr("Reload catalog")
                    onClicked: FreeLibrary.reloadCatalog()
                }
            }
        }

        Rectangle {
            anchors.right: parent.right
            width: 1
            height: parent.height
            color: Theme.panelBorder
        }
    }

    // ─── Right: search + results ────────────────────────────────────────────────────────────
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

            AssetSearchBar {
                id: search
                width: parent.width
                placeholderText: root.activeSource.type === "static"
                                 ? qsTr("Filter %1").arg(root.activeSource.name || "")
                                 : qsTr("Search %1").arg(root.activeSource.name || "")
                text: root.searchText
                onTextChanged: root.searchText = text
            }

            Row {
                width: parent.width
                spacing: Theme.spacingXs
                ThemedTextField {
                    id: linkField
                    width: parent.width - addLink.width - parent.spacing
                    placeholderText: qsTr("Paste a Lottie / video / audio link…")
                    onAccepted: addLink.clicked()
                }
                ThemedButton {
                    id: addLink
                    variant: "secondary"
                    glyph: Theme.icons.download
                    tooltip: qsTr("Download the link and add it to media")
                    enabled: linkField.text.trim().length > 0
                    onClicked: {
                        if (FreeLibrary.addFromUrl(linkField.text))
                            linkField.text = ""
                    }
                }
            }

            Text {
                width: parent.width
                visible: (root.activeSource.license || "").length > 0
                wrapMode: Text.WordWrap
                text: root.activeSource.license || ""
                color: Theme.mutedForeground
                font.pixelSize: Theme.fontSizeXs
            }
        }

        // API key prompt for sources that need one.
        Column {
            visible: root.needsKey
            anchors.top: header.bottom
            anchors.topMargin: Theme.spacingLg
            width: parent.width
            spacing: Theme.spacingSm

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                color: Theme.panelForeground
                font.pixelSize: Theme.fontSizeBase
                text: root.error === "bad-key"
                      ? qsTr("%1 refused the saved API key. Paste a working key.").arg(root.activeSource.name || "")
                      : qsTr("%1 needs a free API key. Create one on their site (it takes a minute), then paste it here.")
                        .arg(root.activeSource.name || "")
            }
            ThemedButton {
                variant: "secondary"
                glyph: Theme.icons.externalLink
                text: qsTr("Get a free key")
                visible: (root.activeSource.keyUrl || "").length > 0
                onClicked: Qt.openUrlExternally(root.activeSource.keyUrl)
            }
            Row {
                width: parent.width
                spacing: Theme.spacingXs
                ThemedTextField {
                    id: keyField
                    width: parent.width - saveKey.width - parent.spacing
                    placeholderText: qsTr("API key")
                    echoMode: TextInput.Password
                    onAccepted: saveKey.clicked()
                }
                ThemedButton {
                    id: saveKey
                    variant: "primary"
                    text: qsTr("Save")
                    enabled: keyField.text.trim().length > 0
                    onClicked: {
                        FreeLibrary.setApiKey(root.activeSource.id, keyField.text)
                        keyField.text = ""
                    }
                }
            }
        }

        GridView {
            id: grid
            visible: !root.needsKey
            anchors.top: header.bottom
            anchors.topMargin: Theme.spacingSm
            anchors.bottom: parent.bottom
            width: parent.width
            clip: true
            readonly property int columns: Math.max(1, Math.floor(width / 150))
            cellWidth: Math.floor(width / columns)
            cellHeight: Math.round((cellWidth - Theme.spacingSm) * 9 / 16) + 30
            model: results
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar {}

            delegate: Item {
                required property var entry
                width: grid.cellWidth
                height: grid.cellHeight
                StockMediaCard {
                    item: parent.entry
                    cardWidth: grid.cellWidth - Theme.spacingSm
                    onAdded: root.added()
                }
            }

            onAtYEndChanged: if (atYEnd) root.loadMore()

            footer: Item {
                width: grid.width
                height: root.loading ? 40 : 0
                CircularProgress {
                    anchors.centerIn: parent
                    visible: root.loading
                    indeterminate: true
                    size: 22
                }
            }
        }

        // Empty / error states
        Text {
            visible: !root.needsKey && !root.loading && results.count === 0
            anchors.centerIn: grid
            width: grid.width * 0.85
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: Theme.mutedForeground
            font.pixelSize: Theme.fontSizeBase
            text: root.error === "bad-key" ? qsTr("The API key was refused. Check it and save it again.")
                : root.error.length > 0 ? root.error
                : root.activeSource.id === "my-lotties"
                  ? qsTr("No Lotties yet. On lottiefiles.com open a free animation, copy its Lottie JSON link and paste it above.")
                  : qsTr("Nothing found.")
        }
    }

    ToolTip {
        id: toast
        x: (root.width - width) / 2
        y: root.height - height - Theme.spacingLg
        timeout: 5000
    }
}
