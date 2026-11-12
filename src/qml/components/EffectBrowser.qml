import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
// Browsable effect preset picker: category chips + card grid.
// Drag a card onto a timeline clip, or click / tap + to apply to the selection.
Column {
    id: root
    spacing: 0

    readonly property string favoritesId: "__favorites__"
    readonly property string mineId: "__mine__"
    // Bumped when an addon changes the effects on disk (an imported .driftfx, a pack install).
    property int catalogTick: 0
    readonly property var categories: { void catalogTick; return EditorState.effectCategories() }
    readonly property var catalog: { void catalogTick; return EditorState.effectCatalog() }
    property string activeCategory: categories.length > 0 ? categories[0].id : ""
    property alias searchText: search.text
    readonly property string query: search.text.trim().toLowerCase()
    property int favoritesTick: 0

    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind === "effects")
                root.catalogTick++
        }
    }

    Connections {
        target: EditorState
        function onAssetFavoritesChanged() {
            root.favoritesTick++
        }
    }

    // Search spans every category — once you have a name, the sectors are in the way.
    readonly property var visiblePresets: {
        void root.favoritesTick
        const q = root.query
        if (q.length > 0) {
            return root.catalog.filter(function(preset) {
                const label = (preset.label || preset.displayName || "").toLowerCase()
                const id = (preset.id || "").toLowerCase()
                return label.indexOf(q) >= 0 || id.indexOf(q) >= 0
            })
        }
        if (root.activeCategory === root.favoritesId) {
            return root.catalog.filter(function(preset) {
                return EditorState.isAssetFavorite("effects", preset.id)
            })
        }
        if (root.activeCategory === root.mineId)
            return root.catalog.filter(function(preset) { return preset.user === true })
        return root.catalog.filter(function(preset) {
            return preset.category === root.activeCategory
        })
    }

    // Dafin: the Adjustment layer card leads every ordinary category and any search it matches.
    readonly property string adjustmentCardLabel: qsTr("Adjustment layer")
    readonly property bool showAdjustmentCard: {
        if (root.query.length > 0)
            return root.adjustmentCardLabel.toLowerCase().indexOf(root.query) >= 0
                   || "adjustment layer".indexOf(root.query) >= 0
        return root.activeCategory !== root.mineId && root.activeCategory !== root.favoritesId
    }

    function applyPreset(effectId) {
        if (EditorState.selectedClip < 0) {
            EditorState.addAdjustmentClipWithEffect(effectId, -1, -1)
            return
        }
        EditorState.addEffect(EditorState.selectedTrack, EditorState.selectedClip, effectId)
    }

    // With the effects pack uninstalled the catalog is empty, activeCategory falls
    // back to "" and the grid rendered nothing — a blank panel with a stray tip line,
    // while the sibling audio and transitions tabs both offered an install CTA.
    // Startup now prompts for these packs and users can decline, so uninstalled is a
    // normal first-run state rather than an edge case.
    EmptyState {
        width: parent.width
        height: visible ? root.height : 0
        visible: root.catalog.length === 0
        glyph: Theme.icons.wand
        title: qsTr("No effects")
        hint: qsTr("Install the Effects pack from Extras to browse presets here.")
        actionText: qsTr("Get extras")
        onActionTriggered: ShellBridge.main.openAddonManager()
    }

    Column {
        visible: root.catalog.length > 0
        width: parent.width
        height: parent.height
        spacing: 0

        AssetSearchBar {
            id: search
            placeholderText: qsTr("Search effects")

            IconButton {
                glyph: Theme.icons.wand
                variant: "ghost"
                buttonSize: Theme.controlHeight
                tooltip: qsTr("Add adjustment layer — click, or drag it onto a track")
                down: adjustmentDrag.pressed

                // Tap adds at the playhead as before; drag places it on a track.
                AssetDragSource {
                    id: adjustmentDrag
                    anchors.fill: parent
                    kind: "adjustment"
                    payload: "videoEffects"
                    label: qsTr("Adjustment layer")
                    glyph: Theme.icons.wand
                    onTapped: EditorState.addAdjustmentClip(-1, -1)
                }
            }

            IconButton {
                glyph: Theme.icons.download
                variant: "ghost"
                buttonSize: Theme.controlHeight
                tooltip: qsTr("Install a custom effect or transition from a .driftfx or zip made in Drift Forge")
                onClicked: ShellBridge.main.importUserPackage("")
            }
        }

        Item {
            width: 1
            height: Theme.spacingSm
        }

        AssetCategoryChips {
            id: categoryChips
            width: parent.width
            categories: root.categories
            activeCategory: root.activeCategory
            showMine: true
            mineLabel: qsTr("My Effects")
            searching: root.query.length > 0
            onCategoryActivated: (categoryId) => root.activeCategory = categoryId
        }

        Item {
            width: parent.width
            height: Math.max(0, parent.height - search.height - Theme.spacingSm
                             - categoryChips.height)

            Text {
                id: emptySearchHint
                x: 12
                y: 12
                width: parent.width - 24
                visible: root.visiblePresets.length === 0 && !root.showAdjustmentCard
                         && !(root.activeCategory === root.mineId && root.query.length === 0)
                text: root.query.length > 0
                      ? qsTr("No effects match “%1”.").arg(search.text.trim())
                      : (root.activeCategory === root.mineId
                         ? qsTr("Nothing here yet. Import a .driftfx or a Drift Forge zip to add your own.")
                         : root.activeCategory === root.favoritesId
                         ? qsTr("No favorites yet. Star presets to save them here.")
                         : qsTr("Nothing in this category."))
                color: Theme.c.effectBrowser.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                wrapMode: Text.WordWrap
            }

            // Nothing imported yet: the Import call to action sits in the middle of the panel.
            EmptyState {
                anchors.centerIn: parent
                width: Math.min(parent.width - Theme.spacing3xl, 260)
                visible: root.activeCategory === root.mineId && root.visiblePresets.length === 0
                         && root.query.length === 0
                compact: true
                glyph: Theme.icons.download
                title: qsTr("%1 is empty").arg(qsTr("My Effects"))
                hint: qsTr("Import a .driftfx or zip made in Drift Forge to add your own.")
                actionText: qsTr("Import")
                onActionTriggered: ShellBridge.main.importUserPackage("")
            }

            FontMetrics {
                id: labelMetrics
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeCard
                font.weight: Font.Medium
            }

            FontMetrics {
                id: badgeMetrics
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs - 1
            }

            // One trailing gap wider than the padded area, so the cards pack from the left
            // exactly as the old Grid did. Rows are a fixed height (two label lines plus the
            // "Built-in" line) since a GridView can't size each row to its tallest card.
            GridView {
                id: presetGrid
                // As many columns as fit at the nominal card size, rounded, with the cards stretched or
                // squeezed a little to fill the row — a fixed card left a column's worth of empty space.
                readonly property int columnCount: Math.max(1, Math.round(width / (Theme.assetCardWidth + Theme.assetCardGap)))
                readonly property real cardSize: Math.floor(width / columnCount) - Theme.assetCardGap
                x: 12
                width: parent.width - 24 + Theme.assetCardGap
                height: parent.height
                topMargin: 12
                bottomMargin: 12
                visible: root.visiblePresets.length > 0 || root.showAdjustmentCard
                clip: true
                reuseItems: true
                boundsBehavior: Flickable.StopAtBounds
                acceptedButtons: Theme.touchUi ? Qt.LeftButton : Qt.NoButton
                ScrollBar.vertical: AppScrollBar { }
                cellWidth: Math.floor(width / columnCount)
                cellHeight: presetGrid.cardSize + 4 + Math.ceil(labelMetrics.height) * 2
                            + 4 + Math.ceil(badgeMetrics.height) + Theme.assetCardGap
                model: root.visiblePresets

                // Dafin: the adjustment layer as a card of its own at the top of the grid, as in
                // Filmora, rather than only the small wand beside the search field.
                header: Item {
                    width: presetGrid.width
                    height: root.showAdjustmentCard ? presetGrid.cellHeight : 0
                    visible: root.showAdjustmentCard

                    Column {
                        width: presetGrid.cardSize
                        spacing: 4
                        opacity: cardDrag.active ? Theme.c.effectPresetCard.dragOpacity : 1

                        Rectangle {
                            width: presetGrid.cardSize
                            height: presetGrid.cardSize
                            radius: Theme.c.effectPresetCard.radius
                            // The colour adjustment layers have on the timeline, so the card
                            // and the clip it makes read as the same thing.
                            color: Theme.c.timelineClipItem.adjustmentVideoBackground
                            border.width: cardDrag.hovered || cardDrag.active
                                          ? Theme.c.effectPresetCard.borderWidth : 0
                            border.color: Theme.c.effectPresetCard.border
                            scale: cardDrag.hovered ? 1.03 : 1.0

                            Behavior on scale {
                                NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                            }

                            IconGlyph {
                                anchors.centerIn: parent
                                glyph: Theme.icons.wand
                                iconSize: Theme.iconSizeXl
                                iconColor: Theme.c.timelineClipItem.foreground
                            }

                            ThemedToolTip {
                                text: qsTr("Adjustment layer — drag it onto the timeline above your clips, or click to add it at the playhead. Effects and colour put on it change everything below it.")
                                visible: cardDrag.hovered && !cardDrag.active
                            }

                            // Click adds at the playhead; a drag places it on a track (between
                            // two tracks to make a new one there), like the wand by the search.
                            AssetDragSource {
                                id: cardDrag
                                anchors.fill: parent
                                kind: "adjustment"
                                payload: "videoEffects"
                                label: root.adjustmentCardLabel
                                glyph: Theme.icons.wand
                                onTapped: EditorState.addAdjustmentClip(-1, -1)
                            }
                        }

                        Text {
                            width: parent.width
                            text: root.adjustmentCardLabel
                            color: Theme.c.effectPresetCard.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeCard
                            font.weight: Font.Medium
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            maximumLineCount: 2
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            width: parent.width
                            text: qsTr("Affects all below")
                            color: Theme.c.effectPresetCard.foregroundMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs - 1
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                        }
                    }
                }

                delegate: EffectPresetCard {
                    cardSize: presetGrid.cardSize
                    onApplyRequested: (effectId) => root.applyPreset(effectId)
                    onDeleteRequested: (preset) => deleteEffectDialog.openFor(preset)
                }
            }
        }
        }

    ThemedDialog {
        id: deleteEffectDialog

        property string effectId: ""
        property string effectLabel: ""
        property int uses: 0

        title: qsTr("Delete effect")
        acceptText: qsTr("Delete")
        acceptVariant: "destructive"
        preferredWidth: Theme.dialogWidthSm
        acceptOnReturn: false

        function openFor(preset) {
            effectId = preset.id
            effectLabel = preset.label
            uses = EditorState.userEffectUses(preset.id)
            open()
        }

        onAccepted: EditorState.deleteUserEffect(deleteEffectDialog.effectId)

        contentItem: Text {
            width: parent ? parent.width : 320
            wrapMode: Text.WordWrap
            text: deleteEffectDialog.uses > 0
                  ? qsTr("Delete “%1” from My Effects? It is used %n time(s) in this project, and those clips will play without it.", "", deleteEffectDialog.uses)
                        .arg(deleteEffectDialog.effectLabel)
                  : qsTr("Delete “%1” from My Effects? You can import its .driftfx file again later.")
                        .arg(deleteEffectDialog.effectLabel)
            color: Theme.c.effectBrowser.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
        }
    }
}
