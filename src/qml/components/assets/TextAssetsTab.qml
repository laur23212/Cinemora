import QtQuick
import QtQuick.Controls.Basic
import Drift
// Text tab: click a style pack to drop a styled text clip on the timeline
// (placeholder copy + inline edit). Timed captions live under Subtitles.
// Templates the user saved from the properties Text tab sit behind the "My templates" chip,
// sorted into their own categories; template files installed from outside sit under "Installed".
Item {
    id: root

    // A clip landed on the timeline. The phone shell closes the sheet on this —
    // the thing you came for is behind it.
    signal added()

    property string section: "builtin"
    // Dafin: My templates filter: "" = all, "__none__" = uncategorised, else a category id.
    property string category: ""

    // A QVariantList from an invokable is not reactive, so each list is refreshed by poking a
    // counter from the controller's signal.
    property int templatesTick: 0
    readonly property var presets: {
        void root.templatesTick
        return EditorState.textTemplates()
    }
    readonly property var builtinPresets: presets.filter(p => p.origin !== "installed")
    readonly property var installedPresets: presets.filter(p => p.origin === "installed")

    property int userPresetsTick: 0
    readonly property var allUserPresets: {
        void root.userPresetsTick
        return EditorState.userTextPresets()
    }
    readonly property var userPresets: allUserPresets.filter(p => root.category === ""
                                                                  || (root.category === "__none__" ? !p.category
                                                                                                   : p.category === root.category))
    property int categoriesTick: 0
    readonly property var categories: {
        void root.categoriesTick
        void root.userPresetsTick
        return EditorState.templateCategories()
    }
    readonly property var activeCategory: {
        for (const c of root.categories)
            if (c.id === root.category)
                return c
        return null
    }

    Connections {
        target: EditorState
        function onUserTextPresetsChanged() { root.userPresetsTick++ }
        function onTextTemplatesChanged() { root.templatesTick++ }
        function onTemplateCategoriesChanged() { root.categoriesTick++ }
    }

    readonly property string styleFileFilter: qsTr("Dafin template (*.drifttextstyle)")
    readonly property string templateFileFilter: qsTr("Dafin title template (*.json)")

    function importStyle() {
        const url = FileDialogs.openFile(qsTr("Import a saved template"), [root.styleFileFilter])
        if (url.toString().length > 0) {
            EditorState.importUserTextPreset(url)
            root.section = "mine"
        }
    }

    // A template file from outside Dafin (for example made by ChatGPT): checked first; problems
    // are shown with a report the user can paste back to its author word for word.
    function installTemplate() {
        const url = FileDialogs.openFile(qsTr("Install a title template"), [root.templateFileFilter])
        if (url.toString().length === 0)
            return
        const result = EditorState.installTextTemplate(url)
        if (result.ok === true)
            root.section = "installed"
        if (result.ok !== true || (result.issues || []).length > 0)
            reportDialog.openWith(result)
    }

    function exportStyle(preset) {
        const url = FileDialogs.saveFile(qsTr("Export text style"), [root.styleFileFilter],
                                         preset.label, "drifttextstyle")
        if (url.toString().length > 0)
            EditorState.exportUserTextPreset(preset.id, url)
    }

    Item {
        id: toolbar
        width: parent.width
        height: Theme.pagePadding + Theme.controlHeight

        Row {
            x: Theme.pagePadding
            y: Theme.pagePadding
            height: Theme.controlHeight
            spacing: Theme.spacingSm

            ThemedChip {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Built-in")
                variant: "outline"
                selected: root.section === "builtin"
                onClicked: root.section = "builtin"
            }

            ThemedChip {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.installedPresets.length > 0
                text: qsTr("Installed · %1").arg(root.installedPresets.length)
                variant: "outline"
                selected: root.section === "installed"
                onClicked: root.section = "installed"
            }

            ThemedChip {
                anchors.verticalCenter: parent.verticalCenter
                text: root.allUserPresets.length > 0 ? qsTr("My templates · %1").arg(root.allUserPresets.length)
                                                     : qsTr("My templates")
                variant: "outline"
                selected: root.section === "mine"
                onClicked: root.section = "mine"
            }
        }

        IconButton {
            anchors.right: parent.right
            anchors.rightMargin: Theme.pagePadding
            y: Theme.pagePadding
            buttonSize: Theme.controlHeight
            glyph: Theme.icons.folderInput
            variant: "ghost"
            tooltip: qsTr("Install or import a template…")
            onClicked: importMenu.popup()
        }

        ThemedContextMenu {
            id: importMenu
            ThemedMenuItem {
                text: qsTr("Install a title template (.json)…")
                icon.name: Theme.icons.folderInput
                onTriggered: root.installTemplate()
            }
            ThemedMenuItem {
                text: qsTr("Import a saved template (.drifttextstyle)…")
                icon.name: Theme.icons.folderInput
                onTriggered: root.importStyle()
            }
        }
    }

    // Dafin: My templates by category. All · Uncategorized · the user's categories · + new.
    Item {
        id: categoryBar
        width: parent.width
        y: toolbar.height + Theme.spacingSm
        height: visible ? Theme.controlHeightSm + Theme.spacingSm : 0
        visible: root.section === "mine"

        Flickable {
            x: Theme.pagePadding
            width: parent.width - Theme.pagePadding * 2
            height: Theme.controlHeightSm
            contentWidth: categoryRow.width
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Row {
                id: categoryRow
                height: parent.height
                spacing: Theme.spacingXs

                ThemedChip {
                    text: qsTr("All")
                    variant: "outline"
                    selected: root.category === ""
                    onClicked: root.category = ""
                }
                ThemedChip {
                    text: qsTr("Uncategorized")
                    variant: "outline"
                    selected: root.category === "__none__"
                    onClicked: root.category = "__none__"
                }
                Repeater {
                    model: root.categories
                    delegate: ThemedChip {
                        required property var modelData
                        text: modelData.count > 0 ? qsTr("%1 · %2").arg(modelData.name).arg(modelData.count) : modelData.name
                        variant: "outline"
                        selected: root.category === modelData.id
                        onClicked: root.category = modelData.id
                    }
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    buttonSize: Theme.controlHeightSm
                    glyph: Theme.icons.plus
                    variant: "ghost"
                    tooltip: qsTr("New category…")
                    onClicked: newCategoryDialog.openWith(qsTr("New category"), "")
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.activeCategory !== null
                    buttonSize: Theme.controlHeightSm
                    glyph: Theme.icons.ellipsis
                    variant: "ghost"
                    tooltip: qsTr("Category options")
                    onClicked: categoryMenu.popup()
                }
            }
        }

        ThemedContextMenu {
            id: categoryMenu
            ThemedMenuItem {
                text: qsTr("Rename category…")
                icon.name: Theme.icons.pencil
                onTriggered: renameCategoryDialog.openWith(qsTr("Rename category"), root.activeCategory ? root.activeCategory.name : "")
            }
            ThemedMenuItem {
                text: qsTr("Delete category…")
                icon.name: Theme.icons.trash
                onTriggered: deleteCategoryDialog.openFor(root.activeCategory)
            }
        }
    }

    // Built-in packs and the user's own styles are separate grids, switched from the toolbar.
    GridView {
        id: packGrid
        // As many columns as fit at the nominal card size, rounded, with the cards stretched or
        // squeezed a little to fill the row — a fixed card left a column's worth of empty space.
        readonly property int columnCount: Math.max(1, Math.round(width / (Theme.assetCardWidth + Theme.assetCardGap)))
        readonly property real cardSize: Math.floor(width / columnCount) - Theme.assetCardGap
        x: Theme.pagePadding
        y: toolbar.height + Theme.spacingSm
        width: parent.width - Theme.pagePadding * 2 + Theme.assetCardGap
        height: parent.height - y
        bottomMargin: Theme.spacing3xl
        clip: true
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        acceptedButtons: Theme.touchUi ? Qt.LeftButton : Qt.NoButton
        ScrollBar.vertical: AppScrollBar { }
        cellWidth: Math.floor(width / columnCount)
        cellHeight: Math.round(cardSize * 0.55) + Theme.spacingSm
                    + Math.ceil(labelMetrics.height) + Theme.assetCardGap
        visible: root.section === "builtin"
        model: root.builtinPresets

        delegate: TextStylePackCard {
            cardSize: packGrid.cardSize
            onAdded: root.added()
        }
    }

    GridView {
        id: userGrid
        // As many columns as fit at the nominal card size, rounded, with the cards stretched or
        // squeezed a little to fill the row — a fixed card left a column's worth of empty space.
        readonly property int columnCount: Math.max(1, Math.round(width / (Theme.assetCardWidth + Theme.assetCardGap)))
        readonly property real cardSize: Math.floor(width / columnCount) - Theme.assetCardGap
        x: Theme.pagePadding
        y: categoryBar.y + categoryBar.height
        width: parent.width - Theme.pagePadding * 2 + Theme.assetCardGap
        height: parent.height - y
        bottomMargin: Theme.spacing3xl
        clip: true
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        acceptedButtons: Theme.touchUi ? Qt.LeftButton : Qt.NoButton
        ScrollBar.vertical: AppScrollBar { }
        cellWidth: Math.floor(width / columnCount)
        cellHeight: Math.round(cardSize * 0.55) + Theme.spacingSm
                    + Math.ceil(labelMetrics.height) + Theme.assetCardGap
        visible: root.section === "mine" && root.userPresets.length > 0
        model: root.userPresets

        delegate: TextStyleUserCard {
            cardSize: userGrid.cardSize
            onAdded: root.added()
            onExportRequested: (preset) => root.exportStyle(preset)
            onRenameRequested: (preset) => renameDialog.openFor(preset)
            onDeleteRequested: (preset) => deleteDialog.openFor(preset)
            onDetailsRequested: (preset) => detailsDialog.openForEdit(preset)
            onDuplicateRequested: (preset) => EditorState.duplicateUserTextPreset(preset.id)
        }
    }

    // Templates installed from files (an external author's work): same cards, remove instead of edit.
    GridView {
        id: installedGrid
        readonly property int columnCount: Math.max(1, Math.round(width / (Theme.assetCardWidth + Theme.assetCardGap)))
        readonly property real cardSize: Math.floor(width / columnCount) - Theme.assetCardGap
        x: Theme.pagePadding
        y: toolbar.height + Theme.spacingSm
        width: parent.width - Theme.pagePadding * 2 + Theme.assetCardGap
        height: parent.height - y
        bottomMargin: Theme.spacing3xl
        clip: true
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        acceptedButtons: Theme.touchUi ? Qt.LeftButton : Qt.NoButton
        ScrollBar.vertical: AppScrollBar { }
        cellWidth: Math.floor(width / columnCount)
        cellHeight: Math.round(cardSize * 0.55) + Theme.spacingSm
                    + Math.ceil(labelMetrics.height) + Theme.assetCardGap
        visible: root.section === "installed"
        model: root.installedPresets

        delegate: TextStyleUserCard {
            cardSize: installedGrid.cardSize
            installed: true
            onAdded: root.added()
            onDeleteRequested: (preset) => EditorState.uninstallTextTemplate(preset.id)
        }
    }

    EmptyState {
        anchors.horizontalCenter: parent.horizontalCenter
        y: toolbar.height + Math.max(0, (parent.height - toolbar.height - height) / 2)
        width: Math.min(parent.width - Theme.spacing3xl, 260)
        visible: root.section === "mine" && root.userPresets.length === 0
        compact: true
        glyph: Theme.icons.type
        title: root.category === "" ? qsTr("No saved templates") : qsTr("Nothing in this category yet")
        hint: qsTr("Style a title, then use “Save as new template…” in its properties to keep it here.")
        actionText: qsTr("Import")
        onActionTriggered: root.importStyle()
    }

    NameDialog {
        id: renameDialog

        property string presetId: ""

        function openFor(preset) {
            presetId = preset.id
            openWith(qsTr("Rename template"), preset.label)
        }

        onSubmitted: name => EditorState.renameUserTextPreset(renameDialog.presetId, name)
    }

    SaveTemplateDialog {
        id: detailsDialog
    }

    NameDialog {
        id: newCategoryDialog
        onSubmitted: name => {
            const id = EditorState.addTemplateCategory(name)
            if (id.length > 0)
                root.category = id
        }
    }

    NameDialog {
        id: renameCategoryDialog
        onSubmitted: name => {
            if (root.activeCategory)
                EditorState.renameTemplateCategory(root.activeCategory.id, name)
        }
    }

    // Deleting a category never deletes templates: they move to another category or to Uncategorized.
    ThemedDialog {
        id: deleteCategoryDialog
        property var target: null
        readonly property var others: target ? root.categories.filter(c => c.id !== target.id) : []
        title: qsTr("Delete category")
        acceptText: qsTr("Delete category")
        acceptVariant: "destructive"
        preferredWidth: Theme.dialogWidthSm
        acceptOnReturn: false

        function openFor(category) {
            if (!category)
                return
            target = category
            moveBox.currentIndex = 0
            open()
        }

        onAccepted: {
            const i = moveBox.currentIndex - 1
            const moveTo = i >= 0 && i < others.length ? others[i].id : ""
            EditorState.removeTemplateCategory(target.id, moveTo)
            root.category = moveTo.length > 0 ? moveTo : ""
        }

        contentItem: Column {
            width: parent ? parent.width : 320
            spacing: Theme.spacingSm
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: deleteCategoryDialog.target
                      ? qsTr("Delete “%1”? Its %n template(s) are kept and moved to:", "", deleteCategoryDialog.target.count)
                            .arg(deleteCategoryDialog.target.name)
                      : ""
                color: Theme.panelForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
            }
            ThemedComboBox {
                id: moveBox
                width: parent.width
                model: [qsTr("Uncategorized")].concat(deleteCategoryDialog.others.map(c => c.name))
            }
        }
    }

    // What the template check found, with the plain-text report ready to paste back to the
    // template's author (ChatGPT, a designer) without retelling it.
    ThemedDialog {
        id: reportDialog
        property var result: ({})
        title: result.ok === true ? qsTr("Template installed") : qsTr("This template can't be installed yet")
        acceptText: qsTr("Copy report")
        rejectText: qsTr("Close")
        preferredWidth: Theme.dialogWidthMd
        acceptOnReturn: false

        function openWith(r) {
            result = r
            open()
        }

        onAccepted: EditorState.copyPlainTextToClipboard(result.report || "")

        contentItem: Column {
            width: parent ? parent.width : 420
            spacing: Theme.spacingSm
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: reportDialog.result.ok === true
                      ? qsTr("“%1” is in Installed. Notes from the check:").arg(reportDialog.result.label || "")
                      : qsTr("Copy the report and send it to whoever made the template, so they can fix it:")
                color: Theme.panelForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
            }
            Flickable {
                width: parent.width
                height: Math.min(260, reportText.implicitHeight)
                contentHeight: reportText.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar { }
                TextEdit {
                    id: reportText
                    width: parent.width
                    readOnly: true
                    selectByMouse: true
                    wrapMode: Text.Wrap
                    text: reportDialog.result.report || ""
                    color: Theme.mutedForeground
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }
        }
    }

    ThemedDialog {
        id: deleteDialog

        property string presetId: ""
        property string presetLabel: ""

        title: qsTr("Delete template")
        acceptText: qsTr("Delete")
        acceptVariant: "destructive"
        preferredWidth: Theme.dialogWidthSm
        acceptOnReturn: false

        function openFor(preset) {
            presetId = preset.id
            presetLabel = preset.label
            open()
        }

        onAccepted: EditorState.deleteUserTextPreset(deleteDialog.presetId)

        contentItem: Text {
            width: parent ? parent.width : 320
            wrapMode: Text.WordWrap
            text: qsTr("Remove “%1” from My templates? Titles already using it keep their look.")
                      .arg(deleteDialog.presetLabel)
            color: Theme.c.textAssetsTab.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
        }
    }
}
