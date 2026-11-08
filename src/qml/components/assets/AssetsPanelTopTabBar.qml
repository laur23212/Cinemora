import QtQuick
import Drift

// Filmora-style category bar across the top of the assets panel: a large icon over a short
// label, the active category tinted with the accent colour. It drives the same tab model as the
// vertical rail (AssetsPanelTabRail) — only the presentation differs — so every tab, its
// Loader and its saved state keep working unchanged.
//
// `order` lists tabIds in display order; "|" inserts a divider. Tabs missing from `order` are
// appended after a divider so nothing becomes unreachable when a tab is added upstream.
Item {
    id: root

    property var tabs: null              // the AssetsPanel ListModel (tabId, icon, …)
    property var tabLabels: ({})
    property var tabIcons: []
    property int activeTab: 0
    property var order: []
    // Dafin: the button a file drag is resting on, lit like a hovered one; -1 for none.
    property int dropHoverIndex: -1

    signal tabSelected(int index)

    readonly property real cellWidth: 68
    implicitHeight: 62
    height: implicitHeight

    Accessible.role: Accessible.PageTabList

    function indexOfTab(tabId) {
        if (!root.tabs)
            return -1
        for (let i = 0; i < root.tabs.count; ++i) {
            if (root.tabs.get(i).tabId === tabId)
                return i
        }
        return -1
    }

    // The tab model index of the category button at (x, y) in this bar's coordinates, or -1 (a
    // divider, the margins). Used to aim a file drag at a tab by its button.
    function tabIndexAt(x, y) {
        const p = tabRow.mapFromItem(root, x, y)
        const cell = tabRow.childAt(p.x, Math.max(0, Math.min(tabRow.height - 1, p.y)))
        if (!cell || cell.isDivider === undefined || cell.isDivider)
            return -1
        return cell.tabIndex
    }

    // [{ index, divider }] built from `order` plus any tab it does not mention.
    readonly property var entries: {
        const out = []
        if (!root.tabs)
            return out
        const seen = {}
        for (let i = 0; i < root.order.length; ++i) {
            const id = root.order[i]
            if (id === "|") {
                if (out.length > 0 && !out[out.length - 1].divider)
                    out.push({ index: -1, divider: true })
                continue
            }
            const idx = root.indexOfTab(id)
            if (idx >= 0 && !seen[id]) {
                seen[id] = true
                out.push({ index: idx, divider: false })
            }
        }
        let addedDivider = false
        for (let j = 0; j < root.tabs.count; ++j) {
            const tabId = root.tabs.get(j).tabId
            if (seen[tabId])
                continue
            if (!addedDivider && out.length > 0 && !out[out.length - 1].divider) {
                out.push({ index: -1, divider: true })
                addedDivider = true
            }
            out.push({ index: j, divider: false })
        }
        return out
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.c.assetsPanel.background
    }

    Flickable {
        id: strip
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingXs
        anchors.rightMargin: Theme.spacingXs
        contentWidth: tabRow.implicitWidth
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        // Mouse wheel scrolls the strip sideways when the panel is too narrow for every tab.
        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: (event) => {
                const delta = event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x
                strip.contentX = Math.max(0, Math.min(strip.contentWidth - strip.width,
                                                      strip.contentX - delta))
            }
        }

        Row {
            id: tabRow
            height: strip.height
            spacing: 0

            Repeater {
                model: root.entries
                delegate: Item {
                    id: cell
                    required property var modelData
                    readonly property bool isDivider: modelData.divider
                    readonly property int tabIndex: modelData.index
                    readonly property string tabId: isDivider ? "" : root.tabs.get(tabIndex).tabId
                    readonly property bool selected: !isDivider && root.activeTab === tabIndex

                    width: isDivider ? Theme.spacingMd : root.cellWidth
                    height: tabRow.height

                    Rectangle {
                        visible: cell.isDivider
                        anchors.centerIn: parent
                        width: Theme.c.assetsPanelTabButton.separatorThickness
                        height: parent.height * 0.5
                        color: Theme.c.assetsPanelTabButton.separator
                    }

                    Rectangle {
                        visible: !cell.isDivider && (hover.hovered || cell.selected
                                                     || root.dropHoverIndex === cell.tabIndex)
                        anchors.fill: parent
                        anchors.margins: 3
                        radius: Theme.radiusSm
                        color: Theme.c.assetsPanel.optionBackgroundHover
                        opacity: cell.selected ? 0.0 : 0.6
                    }

                    Column {
                        visible: !cell.isDivider
                        anchors.centerIn: parent
                        spacing: 4

                        IconGlyph {
                            anchors.horizontalCenter: parent.horizontalCenter
                            glyph: cell.isDivider ? "" : root.tabIcons[root.tabs.get(cell.tabIndex).icon]
                            iconSize: 22
                            iconColor: cell.selected ? Theme.primary : Theme.c.assetsPanel.optionForeground
                        }

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: root.cellWidth - 4
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            text: root.tabLabels[cell.tabId] || ""
                            font.pixelSize: Theme.fontSizeSm
                            font.weight: cell.selected ? Font.DemiBold : Font.Normal
                            color: cell.selected ? Theme.primary : Theme.c.assetsPanel.optionForeground
                        }
                    }

                    HoverHandler { id: hover; enabled: !cell.isDivider; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        enabled: !cell.isDivider
                        onTapped: root.tabSelected(cell.tabIndex)
                    }

                    Accessible.role: cell.isDivider ? Accessible.Separator : Accessible.PageTab
                    Accessible.name: root.tabLabels[cell.tabId] || ""
                    Accessible.checked: cell.selected
                }
            }
        }
    }

    // Hairline under the bar, matching the panel border.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Theme.c.assetsPanel.borderWidth
        color: Theme.c.assetsPanel.railBorder
    }
}
