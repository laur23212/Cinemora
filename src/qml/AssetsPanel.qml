import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import QtQuick.Dialogs
import Drift
PanelFrame {
    id: root

    // Dafin tutorial: guided-tour id and the "?" that opens this panel's page.
    TourTarget { tourId: !root.railHidden ? "panel.assets" : ""; target: root }
    TutorialHelpButton {
        featureId: "asset-panel"
        allowed: !root.railHidden
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 3
    }

    // When true (Android bottom sheet), hide the side tab rail — the phone
    // bottom rail already picks the active library tab.
    property bool sheetMode: false
    // Pins the panel to one tab and drops the rail: the standalone "Library tab" workspace panes.
    property string lockedTab: ""
    readonly property bool locked: lockedTab !== ""
    readonly property bool railHidden: sheetMode || locked
    readonly property var selectedMediaAssetIds: mediaBrowser.item?.selectedAssetIds ?? []

    // One of the click-to-add tabs (text, subtitles, stickers, shapes) put a clip
    // on the timeline. The phone shell closes the sheet on this; desktop, where
    // the panel is docked, simply leaves it unconnected.
    signal addCompleted()
    // The slot paints the docked surface. Sheet mode draws nothing so the
    // sheet chrome stays the only background.
    color: "transparent"
    border.width: 0
    radius: sheetMode ? 0 : Theme.c.assetsPanel.radius

    ThemeSlot {
        anchors.fill: parent
        visible: !root.sheetMode
        z: -1
        slot: "AssetsPanelBackground"
        control: root
        fallback: Component {
            Rectangle {
                anchors.fill: parent
                radius: Theme.c.assetsPanel.radius
                color: Theme.c.assetsPanel.background
                border.width: Theme.c.assetsPanel.borderWidth
                border.color: Theme.c.assetsPanel.border
            }
        }
    }

    function applyPanelRequest() {
        const request = ShellBridge.panelRequest
        if (!request || request.panel !== "assets" || root.locked)
            return
        ShellBridge.panelRequest = null
        root.showTab(request.tab)
    }
    Connections {
        target: ShellBridge
        function onPanelRequestChanged() { root.applyPanelRequest() }
    }

    Component.onCompleted: {
        AssetLibrary.ensureAllMedia()
        root.applyPanelRequest()
        MediaImport.showTitles = root.showTitleSection
    }
    Component.onDestruction: {
        if (MediaImport.showTitles === root.showTitleSection)
            MediaImport.showTitles = null
    }

    // Dafin: a title file dropped anywhere landed in the Titles library; show it there
    // (Text styles side, on "mine" for My templates or "installed").
    function showTitleSection(section) {
        root.rememberTab("text", "section", section)
        LayoutMemory.setLayoutString("assets/text/mode", "builtin")
        if (!root.locked && root.currentTabId !== "text")
            root.showTab("text")
        Qt.callLater(function () {
            const tab = titlesLoader.item
            if (!tab)
                return
            if (tab.setOnline)
                tab.setOnline(false)
            Qt.callLater(function () {
                if (tab.builtinItem && tab.builtinItem.section !== undefined)
                    tab.builtinItem.section = section
            })
        })
    }

    // Imports and reports the outcome. `importUrls` skips anything it cannot
    // probe, so a bad file used to just never appear with no explanation at all.
    // Import policy lives in the MediaImport singleton so surfaces without an AssetsPanel —
    // the home screen, the Android share target — can import too. Kept as a wrapper because
    // several call sites and the DropArea below already speak this name.
    function importUrlsReporting(urls, fromDrop) {
        MediaImport.importUrls(urls, fromDrop)
    }

    // True while an import is running, so the panel can show progress. The folder walk counts:
    // it is the half that can take a while on a deep tree or a sandboxed (portal) mount.
    readonly property bool importing: AssetLibrary.importing || EditorState.bin.importingFolder

    // A single id goes through the existing single-asset add so that case is byte-for-byte the
    // behavior it always was; only an actual multi-selection goes through the batch add, which
    // places each clip back to back in selection order instead of stacking them all at the
    // playhead.
    function requestAddToTimeline(assetIds) {
        if (assetIds.length === 0)
            return

        function runAdd() {
            if (assetIds.length === 1)
                EditorState.addClipFromAsset(AssetLibrary.indexOfId(assetIds[0]))
            else
                EditorState.addClipsFromAssets(assetIds)
        }

        // On a pristine project, the first video/image clip offers to set up the canvas
        // (resolution/orientation) — same flow as dragging onto the timeline (see
        // TimelinePanel.qml/AndroidTimeline.qml). For a multi-selection, offer it from the
        // first asset that actually needs it, not always assetIds[0].
        if (!ShellBridge.main || !ShellBridge.main.configureAndAddAsset) {
            runAdd()
            return
        }
        for (const id of assetIds) {
            const index = AssetLibrary.indexOfId(id)
            if (index >= 0 && EditorState.shouldConfigureProjectForAsset(index)) {
                ShellBridge.main.configureAndAddAsset(index, runAdd)
                return
            }
        }
        runAdd()
    }

    // Asset ids awaiting confirmation in confirmAssetRemoval — a single-element array for a
    // plain right-click, or the whole multi-selection. The label is held separately because
    // the rows are gone by the time the toast reports on them.
    property var pendingRemovalIds: []
    property string pendingRemovalLabel: ""
    property int pendingRemovalClipCount: 0

    // Removing an asset a clip still points at would leave that clip playing but unable to
    // trim past its cut or merge, so refuse rather than confirm — for a bulk removal, refusing
    // the whole batch over one in-use item beats silently dropping it and surprising the user
    // with a smaller removal than they asked for.
    function requestRemoveAsset(assetIds) {
        const names = []
        let clipCount = 0

        for (const id of assetIds) {
            const index = AssetLibrary.indexOfId(id)
            if (index < 0)
                continue

            names.push(AssetLibrary.assetAt(index).name)
            clipCount += EditorState.clipCountForAsset(index)
        }

        if (names.length === 0)
            return

        root.pendingRemovalIds = assetIds
        root.pendingRemovalLabel =
            names.length === 1 ? names[0] : qsTr("%n items", "", names.length)
        root.pendingRemovalClipCount = clipCount
        confirmAssetRemoval.open()
    }

    ThemedDialog {
        id: confirmAssetRemoval
        title: root.pendingRemovalIds.length === 1 ? qsTr("Remove this media?") : qsTr("Remove these items?")
        acceptText: qsTr("Remove")
        acceptVariant: "destructive"
        preferredWidth: Theme.dialogWidthSm
        // Enter must not commit a destructive action.
        acceptOnReturn: false

        contentItem: ThemedLabel {
            width: parent ? parent.width : Theme.dialogWidthSm
            wrapMode: Text.WordWrap
            size: "sm"
            text: root.pendingRemovalClipCount > 0
                ? (root.pendingRemovalClipCount === 1
                    ? qsTr("“%1” is used by 1 clip on the timeline. Removing this media will also remove that clip and any transitions connected to it. The file on disk is not deleted.")
                        .arg(root.pendingRemovalLabel)
                    : qsTr("“%1” is used by %2 clips on the timeline. Removing this media will also remove those clips and any transitions connected to them. The files on disk are not deleted.")
                        .arg(root.pendingRemovalLabel)
                        .arg(root.pendingRemovalClipCount))
                : qsTr("“%1” will be removed from this project. The file on disk is not deleted.")
                    .arg(root.pendingRemovalLabel)
        }

        onAccepted: {
            const removed = root.pendingRemovalClipCount > 0
                ? EditorState.removeAssetsAndClips(root.pendingRemovalIds)
                : EditorState.removeAssets(root.pendingRemovalIds)
            if (removed > 0) {
                Toasts.success(removed === 1
                    ? qsTr("Removed “%1”.").arg(root.pendingRemovalLabel)
                    : qsTr("Removed %n items.", "", removed))
            }
            root.pendingRemovalIds = []
        }
        onRejected: root.pendingRemovalIds = []
    }

    property int pendingRenameIndex: -1

    function requestRenameAsset(assetIndex) {
        const asset = AssetLibrary.assetAt(assetIndex)
        if (!asset || Object.keys(asset).length === 0)
            return
        root.pendingRenameIndex = assetIndex
        assetRenameField.text = asset.name || ""
        assetRenameDialog.open()
    }

    ThemedDialog {
        id: assetRenameDialog
        title: qsTr("Rename media")
        acceptText: qsTr("Rename")
        preferredWidth: Theme.dialogWidthSm

        contentItem: Column {
            width: parent ? parent.width : Theme.dialogWidthSm
            spacing: Theme.spacingMd

            ThemedLabel {
                width: parent.width
                text: qsTr("Name")
                size: "sm"
            }
            ThemedTextField {
                id: assetRenameField
                width: parent.width
                placeholderText: qsTr("Media name")
            }
        }

        onOpened: {
            assetRenameField.forceActiveFocus()
            assetRenameField.selectAll()
        }
        onAccepted: {
            if (root.pendingRenameIndex < 0)
                return
            const label = assetRenameField.text.trim()
            if (label.length > 0)
                EditorState.renameAsset(root.pendingRenameIndex, label)
            root.pendingRenameIndex = -1
        }
        onRejected: root.pendingRenameIndex = -1
    }

    ThemedDialog {
        id: newFolderDialog
        title: qsTr("New folder")
        acceptText: qsTr("Create")
        preferredWidth: Theme.dialogWidthSm

        contentItem: Column {
            width: parent ? parent.width : Theme.dialogWidthSm
            spacing: Theme.spacingMd

            ThemedLabel {
                width: parent.width
                text: qsTr("Name")
                size: "sm"
            }
            ThemedTextField {
                id: newFolderNameField
                width: parent.width
                placeholderText: qsTr("Folder name")
            }
        }

        onOpened: {
            newFolderNameField.text = ""
            newFolderNameField.forceActiveFocus()
        }
        onAccepted: {
            const label = newFolderNameField.text.trim()
            if (label.length > 0)
                EditorState.bin.createFolder(label, EditorState.bin.currentFolderId)
        }
    }

    property string pendingFolderRenameId: ""

    function requestRenameFolder(folderId, folderName) {
        root.pendingFolderRenameId = folderId
        folderRenameField.text = folderName || ""
        folderRenameDialog.open()
    }

    ThemedDialog {
        id: folderRenameDialog
        title: qsTr("Rename folder")
        acceptText: qsTr("Rename")
        preferredWidth: Theme.dialogWidthSm

        contentItem: Column {
            width: parent ? parent.width : Theme.dialogWidthSm
            spacing: Theme.spacingMd

            ThemedLabel {
                width: parent.width
                text: qsTr("Name")
                size: "sm"
            }
            ThemedTextField {
                id: folderRenameField
                width: parent.width
                placeholderText: qsTr("Folder name")
            }
        }

        onOpened: {
            folderRenameField.forceActiveFocus()
            folderRenameField.selectAll()
        }
        onAccepted: {
            if (root.pendingFolderRenameId.length === 0)
                return
            const label = folderRenameField.text.trim()
            if (label.length > 0)
                EditorState.bin.renameFolder(root.pendingFolderRenameId, label)
            root.pendingFolderRenameId = ""
        }
        onRejected: root.pendingFolderRenameId = ""
    }

    // The "move to folder" path — right-click on a card (or a multi-selection), choose a
    // destination from a flat list. Also doubles as the folder-move picker (pendingMoveFolderId)
    // for reparenting a folder itself; the two are mutually exclusive, never both set.
    property var pendingMoveAssetIds: []
    // The folder every selected asset is in right now, so the picker can omit it — moving them
    // "into" the folder they're already in isn't a real destination. A single common value is
    // safe here (not a per-asset lookup): MediaAssetsTab's grid only ever shows one folder's
    // contents at a time, so anything selectable there already shares this folder.
    property string pendingMoveAssetCurrentFolderId: ""
    // Non-empty while the picker is choosing a new parent for this folder rather than a
    // destination for assets.
    property string pendingMoveFolderId: ""

    function requestMoveAssetToFolder(assetIds) {
        root.pendingMoveFolderId = ""
        root.pendingMoveAssetIds = assetIds
        root.pendingMoveAssetCurrentFolderId = EditorState.bin.currentFolderId
        folderPickerDialog.open()
    }

    function requestMoveFolder(folderId) {
        root.pendingMoveAssetIds = []
        root.pendingMoveFolderId = folderId
        folderPickerDialog.open()
    }

    // True if candidateId is folderId itself or nested anywhere inside it — walked the same
    // way BinBreadcrumb.qml walks a trail, with the same cycle guard folderPath() uses, since
    // this runs on the same possibly-malformed parentId chains.
    function isFolderOrDescendant(candidateId, folderId) {
        const visited = new Set()
        let id = candidateId
        while (id !== "" && !visited.has(id)) {
            if (id === folderId)
                return true
            visited.add(id)
            const folder = BinFolderModel.folderById(id)
            if (!folder || Object.keys(folder).length === 0)
                break
            id = folder.parentId
        }
        return false
    }

    // Matches BinBreadcrumb.qml's own separator glyph, so a folder's path reads the same way
    // here as it does in the "where you are" trail above the grid.
    readonly property string folderPathSeparator: ">"

    // Full path from the bin root down to folderId, e.g. "Interviews > B-Roll" — walks
    // parentId the same way BinBreadcrumb.qml does, since a flat name alone can't
    // distinguish two same-named folders nested under different parents.
    function folderPath(folderId) {
        const names = []
        const visited = new Set()
        let id = folderId
        // Project deserialization doesn't reject a self- or mutually-parented folder, and
        // this runs once per folder every time the picker opens — an undetected cycle would
        // spin this loop forever and hang the UI, so bail the moment an id repeats.
        while (id !== "" && !visited.has(id)) {
            visited.add(id)
            const folder = BinFolderModel.folderById(id)
            if (!folder || Object.keys(folder).length === 0)
                break
            names.unshift(folder.name)
            id = folder.parentId
        }
        return names.join(" " + root.folderPathSeparator + " ")
    }

    ThemedDialog {
        id: folderPickerDialog
        title: qsTr("Move to folder")
        showFooter: false
        preferredWidth: Theme.dialogWidthSm

        // Flat list, root first, minus destinations that aren't real moves. Each entry shows
        // its full path rather than just its own name, so two folders that happen to share a
        // name (nested under different parents) still read as distinct destinations.
        //
        // folderAt() is a plain invokable call, not a property read, so it isn't by itself
        // enough to make this binding re-evaluate after a rename (BinFolderModel.count doesn't
        // change either). Reading undoAvailable is a cheap way to add that dependency: its
        // NOTIFY is undoStackChanged, which fires after every project edit including a rename.
        readonly property var folderOptions: {
            void EditorState.undoAvailable
            const movingFolderId = root.pendingMoveFolderId
            const out = []
            if (movingFolderId !== "") {
                // Moving a folder itself: exclude the folder, anything already its parent (no-op),
                // and every one of its own descendants — landing there would create a cycle.
                const currentParentId = BinFolderModel.folderById(movingFolderId).parentId || ""
                if (currentParentId !== "")
                    out.push({ id: "", name: qsTr("Media"), path: qsTr("Media") })
                for (let i = 0; i < BinFolderModel.count; ++i) {
                    const folder = BinFolderModel.folderAt(i)
                    if (folder.id === currentParentId)
                        continue
                    if (root.isFolderOrDescendant(folder.id, movingFolderId))
                        continue
                    out.push({ id: folder.id, name: folder.name, path: root.folderPath(folder.id) })
                }
                return out
            }

            const currentFolderId = root.pendingMoveAssetCurrentFolderId
            if (currentFolderId !== "")
                out.push({ id: "", name: qsTr("Media"), path: qsTr("Media") })
            for (let i = 0; i < BinFolderModel.count; ++i) {
                const folder = BinFolderModel.folderAt(i)
                if (folder.id !== currentFolderId)
                    out.push({ id: folder.id, name: folder.name, path: root.folderPath(folder.id) })
            }
            return out
        }

        // A Rectangle used directly as contentItem never reports its explicit `height` as
        // `implicitHeight`, so the Dialog (which sizes off contentItem.implicitHeight) sees zero
        // and clips the list away entirely. Wrapping in a Column — which does propagate its
        // children's real heights into implicitHeight — is the same fix LanguageChooserDialog
        // already uses for the identical list-in-a-dialog shape.
        contentItem: Column {
            width: parent ? parent.width : Theme.dialogWidthSm

            Rectangle {
                width: parent.width
                height: Math.min(pickerList.contentHeight + 2, 280)
                radius: Theme.c.assetsPanel.pickerRadius
                color: Theme.c.assetsPanel.pickerBackground
                border.width: Theme.c.assetsPanel.pickerBorderWidth
                border.color: Theme.c.assetsPanel.pickerBorder
                clip: true

                ListView {
                    id: pickerList
                    anchors.fill: parent
                    anchors.margins: Theme.c.assetsPanel.pickerBorderWidth
                    clip: true
                    model: folderPickerDialog.folderOptions
                    interactive: contentHeight > height
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: AppScrollBar { }

                    delegate: ItemDelegate {
                        id: optionRow
                        required property var modelData
                        width: pickerList.width
                        height: 40
                        hoverEnabled: true

                        HoverHandler {
                            cursorShape: Qt.PointingHandCursor
                        }

                        background: Rectangle {
                            color: optionRow.hovered ? Theme.c.assetsPanel.optionBackgroundHover : "transparent"
                        }

                        contentItem: Text {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            verticalAlignment: Text.AlignVCenter
                            text: optionRow.modelData.path
                            elide: Text.ElideRight
                            color: Theme.c.assetsPanel.optionForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                        }

                        onClicked: {
                            // Closed before the move runs, not after: moving a folder changes
                            // undoAvailable, which folderOptions depends on, which swaps the
                            // ListView's model out from under this very delegate — closing
                            // first avoids racing that live update instead of fighting it.
                            const targetId = optionRow.modelData.id
                            folderPickerDialog.close()
                            if (root.pendingMoveFolderId !== "") {
                                EditorState.bin.moveFolder(root.pendingMoveFolderId, targetId)
                                root.pendingMoveFolderId = ""
                            } else if (root.pendingMoveAssetIds.length > 0) {
                                EditorState.bin.moveAssetsToFolder(root.pendingMoveAssetIds, targetId)
                                root.pendingMoveAssetIds = []
                            }
                        }
                    }
                }
            }
        }
    }

    // Points a bin row at a different file while every clip using it stays put, so a project set
    // up once — music, outro, CTA — can be re-pointed at the next video instead of rebuilt.
    function requestReplaceAsset(assetIndex) {
        var url = FileDialogs.openFile(qsTr("Replace Media"), [AssetLibrary.mediaNameFilter()])
        if (!url || url.toString() === "")
            return
        EditorState.bin.replaceSource(assetIndex, url)
    }

    function requestLinkProxy(assetId) {
        var url = FileDialogs.openFile(qsTr("Link Proxy"), [AssetLibrary.mediaNameFilter()])
        if (!url || url.toString() === "")
            return
        AssetLibrary.linkProxy(assetId, url)
    }

    property string pendingProxyPattern: ""
    property int pendingProxyMatches: 0

    ThemedDialog {
        id: siblingRelinkDialog
        property int count: 0
        property string folder: ""
        title: qsTr("Relink other missing files?")
        acceptText: qsTr("Relink")
        preferredWidth: Theme.dialogWidthSm

        contentItem: ThemedLabel {
            width: parent ? parent.width : Theme.dialogWidthSm
            wrapMode: Text.WordWrap
            size: "sm"
            text: qsTr("%n other missing files from the same folder were found in “%1” with the same name and size. Relink them too?", "", siblingRelinkDialog.count)
                .arg(siblingRelinkDialog.folder)
        }

        onAccepted: EditorState.bin.relinkSiblings()
    }

    ThemedDialog {
        id: linkProxyPatternDialog
        title: qsTr("Link proxies by pattern?")
        acceptText: qsTr("Link")
        preferredWidth: Theme.dialogWidthSm

        contentItem: ThemedLabel {
            width: parent ? parent.width : Theme.dialogWidthSm
            wrapMode: Text.WordWrap
            size: "sm"
            text: qsTr("“%1” matches this clip and %n other clips in the project. Drift will link them all, and link future imports whose proxy follows the same name.", "", root.pendingProxyMatches)
                .arg(root.pendingProxyPattern)
        }

        onAccepted: {
            AssetLibrary.commitProxyRule(root.pendingProxyPattern)
            Toasts.success(qsTr("Linked this clip and %n other clips.", "", root.pendingProxyMatches))
        }
    }

    ThemedDialog {
        id: proxyFallbackDialog
        property string folders: ""
        title: qsTr("Couldn’t save files next to the media")
        acceptText: qsTr("OK")
        showReject: false
        preferredWidth: Theme.dialogWidthSm

        contentItem: Column {
            width: parent ? parent.width : Theme.dialogWidthSm
            spacing: Theme.spacingMd

            ThemedLabel {
                width: parent.width
                wrapMode: Text.WordWrap
                size: "sm"
                text: qsTr("Drift can’t write to the folders listed below, because of their permissions or because they are on a read-only network share. The proxies or reversed clips were saved to Drift’s cache instead.")
            }
            TextEdit {
                width: parent.width
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                color: Theme.c.assetsPanel.fallbackForeground
                selectionColor: Theme.c.assetsPanel.fallbackSelection
                selectedTextColor: Theme.c.assetsPanel.fallbackSelectionForeground
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontSizeSm
                text: proxyFallbackDialog.folders
            }
        }
    }

    Connections {
        target: AssetLibrary

        function onProxyLinkProbed(assetId, pattern, otherMatches, error) {
            if (error.length > 0) {
                Toasts.error(error)
            } else if (otherMatches === 0) {
                AssetLibrary.commitProxyRule(pattern)
                Toasts.success(qsTr("Linked the proxy. Future imports named like “%1” will link automatically.").arg(pattern))
            } else {
                root.pendingProxyPattern = pattern
                root.pendingProxyMatches = otherMatches
                linkProxyPatternDialog.open()
            }
        }
        function onProxyLocationFallback(folders) {
            proxyFallbackDialog.folders = folders.join("\n")
            proxyFallbackDialog.open()
        }
    }

    // Writes an image row back out to disk — the way a freeze frame captured in the preview
    // leaves the project. The format follows the name the user picks, not the filter.
    function requestExportAsset(assetIndex) {
        const asset = AssetLibrary.assetAt(assetIndex)
        if (!asset || asset.kind !== "image")
            return
        var url = FileDialogs.saveFile(qsTr("Export Image"), [
            qsTr("PNG image (*.png)"),
            qsTr("JPEG image (*.jpg *.jpeg)")
        ], asset.name, "png")
        if (!url || url.toString() === "")
            return
        if (EditorState.bin.exportImage(assetIndex, url))
            Toasts.success(qsTr("Exported “%1”.").arg(asset.name))
        else
            Toasts.error(qsTr("Couldn’t export that image."))
    }

    // Copies the file behind a bin row out to disk as is, no transcode.
    function requestSaveAssetAs(assetIndex) {
        const asset = AssetLibrary.assetAt(assetIndex)
        if (!asset || !asset.path)
            return
        const fileName = asset.path.substring(asset.path.lastIndexOf("/") + 1)
        const dot = fileName.lastIndexOf(".")
        const suffix = dot > 0 ? fileName.substring(dot + 1) : ""
        let baseName = asset.name || (dot > 0 ? fileName.substring(0, dot) : fileName)
        if (suffix && baseName.toLowerCase().endsWith("." + suffix.toLowerCase()))
            baseName = baseName.substring(0, baseName.length - suffix.length - 1)
        const filters = suffix ? [qsTr("%1 file (*.%2)").arg(suffix.toUpperCase()).arg(suffix)]
                               : [qsTr("All files (*)")]
        var url = FileDialogs.saveFile(qsTr("Save As"), filters, baseName, suffix)
        if (!url || url.toString() === "")
            return
        if (EditorState.bin.saveAs(assetIndex, url))
            Toasts.info(qsTr("Saving “%1”…").arg(asset.name))
        else
            Toasts.error(qsTr("Couldn’t save “%1”.").arg(asset.name))
    }

    Connections {
        target: EditorState.bin

        // Hitting the limit outranks the skipped count: the walk stopped early, so what it passed
        // over is only part of the story and saying both would suggest otherwise.
        function onFolderImportFinished(folders, files, skipped, truncated) {
            if (folders === 0) {
                Toasts.error(qsTr("Couldn’t import that folder."))
            } else if (truncated) {
                Toasts.warning(qsTr("Imported %n files into %1 folders — as many as one folder import takes. Import the remaining subfolders separately.", "", files).arg(folders))
            } else if (skipped > 0) {
                Toasts.warning(qsTr("Imported %n files into %1 folders. %2 files were skipped — Drift does not recognize their format. Drag them onto the bin to try anyway.", "", files).arg(folders).arg(skipped))
            } else {
                Toasts.success(qsTr("Imported %n files into %1 folders.", "", files).arg(folders))
            }
        }

        function onSaveFinished(ok, name) {
            if (ok)
                Toasts.success(qsTr("Saved “%1”.").arg(name))
            else
                Toasts.error(qsTr("Couldn’t save “%1”.").arg(name))
        }

        // The probe runs off-thread, so the outcome comes back here rather than from the call.
        function onReplaceFinished(ok, message, adjustedClips) {
            if (!ok) {
                Toasts.warning(message)
            } else if (adjustedClips > 0) {
                Toasts.warning(qsTr("Replaced with “%1”. %n clips were shortened to fit the new file.",
                                    "", adjustedClips).arg(message))
            } else {
                Toasts.success(qsTr("Replaced with “%1”.").arg(message))
            }
        }

        function onSiblingRelinkAvailable(count, folder) {
            siblingRelinkDialog.count = count
            siblingRelinkDialog.folder = folder
            siblingRelinkDialog.open()
        }

        function onEditFinished(ok, message) {
            if (!ok) {
                if (message && message.length > 0)
                    Toasts.warning(message)
            } else if (EditorState.bin.editIsConversion) {
                Toasts.success(qsTr("“%1” is now in an edit-friendly format.").arg(message))
            } else {
                Toasts.success(qsTr("Saved “%1”. Drag it onto the timeline.").arg(message))
            }
        }
    }

    function importMedia() {
        var urls = FileDialogs.openFiles(qsTr("Import Media"),
                                         [AssetLibrary.mediaNameFilter(),
                                          qsTr("All Files (*)")])
        root.importUrlsReporting(urls)
    }

    // Imports a whole directory: a new bin folder mirrors the picked folder (and everything
    // nested under it), and every media file lands in the bin folder matching its containing
    // directory. EditorState.bin.importFolder does the walk synchronously — probing and
    // thumbnailing each file still happens in the background the same as any other import.
    function importFolder() {
        var url = FileDialogs.openDirectory(qsTr("Import Folder"))
        if (!url || url.toString() === "")
            return
        // The walk runs off-thread, so the outcome arrives as onFolderImportFinished below.
        if (!EditorState.bin.importFolder(url))
            Toasts.error(qsTr("Couldn’t import that folder."))
    }

    function collectMedia() {
        var url = FileDialogs.openDirectory(qsTr("Collect Media to Folder"))
        if (!url || url.toString() === "")
            return
        collectMediaDialog.folder = url
        collectMediaDialog.open()
    }

    CollectMediaDialog {
        id: collectMediaDialog
    }

    // Selects a tab by id. Used by cross-panel jumps such as the properties
    // panel's "Browse effects" / "Browse audio effects" empty-state actions.
    function showTab(tabId) {
        if (root.locked)
            return
        for (var i = 0; i < tabsModel.count; ++i) {
            if (tabsModel.get(i).tabId === tabId) {
                root.activeTab = i
                return
            }
        }
    }

    function tabIndexOf(tabId) {
        for (let i = 0; i < tabsModel.count; ++i) {
            if (tabsModel.get(i).tabId === tabId)
                return i
        }
        return 0
    }

    function tabLabel(tabId) {
        return tabLabels[tabId] || ""
    }

    function kindsForTab(tabId) {
        if (tabId === "media") return ["video", "image", "audio", "vector", "model3d", "pdf"]
        return []
    }

    function assetVisible(kind) {
        const tabId = tabsModel.get(activeTab).tabId
        if (tabId === "stock" || tabId === "text" || tabId === "subtitles" || tabId === "speech" || tabId === "stickers" || tabId === "shapes"
                || tabId === "effects" || tabId === "templates" || tabId === "adjustment"
                || tabId === "sounds" || tabId === "transitions" || tabId === "masks"
                || tabId === "scenes" || tabId === "ai")
            return false
        const kinds = kindsForTab(tabId)
        return kinds.length === 0 || kinds.indexOf(kind) >= 0
    }

    // ListElement only accepts literal values; qsTr() calls are not
    // evaluated. Labels are translated via tabLabels below.
    property var tabLabels: ({
        "media": qsTr("Media"),
        "stock": root.topTabs ? qsTr("Stock Video") : qsTr("Stock Media"),
        "text": root.topTabs ? qsTr("Titles") : qsTr("Text"),
        "subtitles": qsTr("Subtitles"),
        "speech": qsTr("Speech"),
        "stickers": qsTr("Stickers"),
        "shapes": qsTr("Shapes"),
        "scenes": qsTr("Scenes"),
        "masks": qsTr("Masks"),
        "effects": qsTr("Effects"),
        "templates": qsTr("Templates"),
        "transitions": qsTr("Transitions"),
        "sounds": root.topTabs ? qsTr("Audio") : qsTr("Audio FX"),
        "ai": qsTr("AI")
    })

    // Rail order: project media → on-canvas graphics → processing → prefs.
    // `separatorAfter` draws a hairline under the tab so groups read as sections.
    // tabId "sounds" is kept for favorites persistence (settings key).
    ListModel {
        id: tabsModel
        ListElement { tabId: "media"; icon: 0; separatorAfter: false }
        ListElement { tabId: "stock"; icon: 14; separatorAfter: true }
        ListElement { tabId: "text"; icon: 1; separatorAfter: false }
        ListElement { tabId: "subtitles"; icon: 2; separatorAfter: false }
        ListElement { tabId: "speech"; icon: 13; separatorAfter: false }
        ListElement { tabId: "stickers"; icon: 3; separatorAfter: false }
        ListElement { tabId: "shapes"; icon: 4; separatorAfter: false }
        ListElement { tabId: "masks"; icon: 11; separatorAfter: true }
        ListElement { tabId: "scenes"; icon: 10; separatorAfter: true }
        ListElement { tabId: "effects"; icon: 5; separatorAfter: false }
        ListElement { tabId: "templates"; icon: 6; separatorAfter: false }
        ListElement { tabId: "transitions"; icon: 7; separatorAfter: false }
        ListElement { tabId: "sounds"; icon: 8; separatorAfter: true }
        // Dafin: AI tools that run on this computer.
        ListElement { tabId: "ai"; icon: 15; separatorAfter: true }
    }
    property var tabIcons: [
        Theme.icons.film,
        Theme.icons.type,
        Theme.icons.captions,
        Theme.icons.smile,
        Theme.icons.shapes,
        Theme.icons.wand,
        Theme.icons.layers,
        Theme.icons.chevronsRight,
        Theme.icons.audioLines,
        Theme.icons.keyboard,
        Theme.icons.listVideo,
        Theme.icons.mask,
        Theme.icons.store,
        Theme.icons.micVocal,
        Theme.icons.download,
        Theme.icons.sparkles
    ]

    // Filmora-style horizontal category bar instead of the vertical rail. Remembered per user;
    // the phone sheet and locked single-tab panels never show it.
    property bool topTabs: !root.railHidden && LayoutMemory.layoutString("assets/tabStyle", "top") === "top"
    function setTopTabs(on) {
        LayoutMemory.setLayoutString("assets/tabStyle", on ? "top" : "rail")
        root.topTabs = Qt.binding(() => !root.railHidden
                                  && LayoutMemory.layoutString("assets/tabStyle", "top") === "top")
    }
    // Filmora's order first; Drift's extra tabs follow after the divider.
    readonly property var topTabOrder: ["media", "stock", "sounds", "text", "transitions", "effects",
                                        "stickers", "templates", "ai", "|", "subtitles", "speech", "shapes",
                                        "masks", "scenes"]
    property int activeTab: locked ? tabIndexOf(lockedTab) : tabIndexOf(LayoutMemory.layoutString("assets/tab", "media"))
    readonly property string currentTabId: tabsModel.get(activeTab).tabId

    // Per-tab view state that outlives the tab being unloaded, keyed by tabId. Plain
    // storage: tabs read it once on creation and write each change back.
    property var tabState: ({})

    function rememberTab(tabId, key, value) {
        if (!root.tabState[tabId])
            root.tabState[tabId] = {}
        root.tabState[tabId][key] = value
    }

    // Search text and category for the catalog browsers. A category that went away
    // while the tab was unloaded (addon removed) falls back to the tab's default.
    function restoreBrowserTab(tab, tabId) {
        const saved = root.tabState[tabId] || {}
        if (saved.search)
            tab.searchText = saved.search
        if (saved.category === undefined || !tab.categories)
            return
        if (saved.category === "__favorites__" || saved.category === "__mine__"
                || tab.categories.some(category => category.id === saved.category))
            tab.activeCategory = saved.category
    }

    // Fades the tab body in on a tab change instead of hard-cutting to it. Driven
    // as one property the bodies share, rather than fading the whole content
    // Column, so the panel header does not flash along with it. Fade-in only, not
    // a crossfade: the bodies are Column siblings, and overlapping two would
    // double-count height and jump the layout mid-transition.
    property real tabOpacity: 1.0

    // Rail width is saved in tab-cell units so it follows UI scale. The window can
    // only clamp what is shown; the saved value is rewritten by a drag alone.
    property real railUnits: Math.max(1, LayoutMemory.layoutNumber("assets/railUnits", 1))
    property real railDragWidth: -1
    readonly property real maxRailWidth: Math.max(Theme.tabRailWidth,
        root.width - Theme.assetCardWidth * 2 - Theme.pagePadding * 2)
    readonly property real railWidth: Math.max(Theme.tabRailWidth,
        Math.min(maxRailWidth, railDragWidth >= 0 ? railDragWidth : railUnits * Theme.tabRailWidth))

    onActiveTabChanged: {
        if (!root.locked)
            LayoutMemory.setLayoutString("assets/tab", root.currentTabId)
        root.tabOpacity = 0
        tabFadeIn.restart()
    }

    NumberAnimation {
        id: tabFadeIn
        target: root
        property: "tabOpacity"
        from: 0.0
        to: 1.0
        duration: Theme.durationBase
        easing.type: Theme.easing
    }

    // Dafin: the tabs that keep the user's own files. Titles takes .mogrt templates (or a .zip
    // holding one and its preview video), Stickers Lottie animations, Audio sound files.
    function userCollectionForTab(tabId) {
        if (tabId === "text") return "titles"
        if (tabId === "stickers") return "lotties"
        if (tabId === "sounds") return "sounds"
        return ""
    }

    // The tab a drag at (x, y) in panel coordinates is aimed at: the category button under the
    // pointer in the top bar, else the open tab.
    function dropTabAt(x, y) {
        if (root.topTabs && topTabBar.visible) {
            const p = topTabBar.mapFromItem(root, x, y)
            if (p.y >= 0 && p.y < topTabBar.height) {
                const index = topTabBar.tabIndexAt(p.x, p.y)
                if (index >= 0)
                    return tabsModel.get(index).tabId
            }
        }
        return root.currentTabId
    }

    readonly property var userTabLoaders: ({ "text": titlesLoader, "stickers": stickersLoader, "sounds": soundsLoader })

    // Files dropped on Titles, Stickers or Audio join the user's library for that tab and the tab
    // shows them; anything the tab does not take is imported into Media as before.
    //
    // One DropArea decides this, at drop time. The tabs used to carry DropAreas of their own that
    // refused a drag in onEntered when it held nothing for them; on Windows that check never let a
    // drag through, so Lotties fell through to the Media import and templates were refused
    // outright. The URLs are always readable once the drop has happened, so the choice is made then.
    function routeDroppedUrls(urls, tabId) {
        // Dafin: title templates and exported title styles go to Titles from any tab; a .json
        // dropped on Stickers is only a Lottie if it is not one of those.
        urls = MediaImport.takeTitleFiles(urls, root.showTitleSection)
        if (urls.length === 0)
            return
        const collection = root.userCollectionForTab(tabId)
        if (collection.length === 0 || !OnlineAssets.userCollectionAccepts(collection, urls)) {
            root.importUrlsReporting(urls, true)
            return
        }
        if (!root.locked && root.currentTabId !== tabId)
            root.showTab(tabId)
        // The online side is where My Lotties, My sounds and the imported templates are listed.
        LayoutMemory.setLayoutString("assets/" + tabId + "/mode", "online")
        const loader = root.userTabLoaders[tabId]
        if (loader && loader.item && loader.item.setOnline)
            loader.item.setOnline(true)
        const result = OnlineAssets.importUserFiles(collection, urls)
        if (result.added > 0 || result.duplicates > 0) {
            Qt.callLater(function () {
                if (loader && loader.item && loader.item.showUserFiles)
                    loader.item.showUserFiles()
            })
        }
        // Media files in the same drop still go where they always went.
        const rest = OnlineAssets.urlsNotAcceptedBy(collection, urls)
        if (rest.length > 0)
            root.importUrlsReporting(rest, true)
    }

    // Tab the current drag is aimed at, for the overlay text. A drag that rests on a category
    // button opens that tab, as Filmora does.
    property string dragTabId: ""
    Timer {
        id: dragTabSwitch
        interval: 550
        onTriggered: {
            if (assetDropArea.containsDrag && root.dragTabId.length > 0 && root.dragTabId !== root.currentTabId)
                root.showTab(root.dragTabId)
        }
    }

    DropArea {
        id: assetDropArea
        anchors.fill: parent
        enabled: !root.locked || root.kindsForTab(root.currentTabId).length > 0
                 || root.userCollectionForTab(root.currentTabId).length > 0
        keys: ["text/uri-list"]
        function track(drag) {
            const tabId = root.dropTabAt(drag.x, drag.y)
            if (tabId !== root.dragTabId) {
                root.dragTabId = tabId
                if (tabId !== root.currentTabId)
                    dragTabSwitch.restart()
                else
                    dragTabSwitch.stop()
            }
        }
        // A sticker or title card dragged as its file (AssetDragSource.fileUrl) is on its way to
        // the timeline or the preview; passing back over the panel must not file it again.
        readonly property bool internalDrag: AssetDrag.current.kind === "file"
        onEntered: (drag) => {
            if (assetDropArea.internalDrag) {
                drag.accepted = false
                return
            }
            drag.accept(Qt.CopyAction)
            track(drag)
        }
        onPositionChanged: (drag) => {
            if (!assetDropArea.internalDrag)
                track(drag)
        }
        onExited: {
            dragTabSwitch.stop()
            root.dragTabId = ""
        }
        onDropped: (drop) => {
            dragTabSwitch.stop()
            const tabId = root.dropTabAt(drop.x, drop.y)
            root.dragTabId = ""
            if (!drop.hasUrls || assetDropArea.internalDrag)
                return
            drop.accept(Qt.CopyAction)
            root.routeDroppedUrls(drop.urls, tabId)
        }
    }

    AssetsPanelDropOverlay {
        active: assetDropArea.containsDrag
        collection: root.userCollectionForTab(root.dragTabId.length > 0 ? root.dragTabId : root.currentTabId)
        collectionName: collection.length > 0 ? OnlineAssets.userCollectionName(collection) : ""
    }

    AssetsPanelImportOverlay {
        active: root.importing && assetDropArea.enabled
    }

    AssetsPanelTopTabBar {
        id: topTabBar
        visible: root.topTabs
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.c.assetsPanel.borderWidth
        height: visible ? implicitHeight : 0
        tabs: tabsModel
        tabLabels: root.tabLabels
        tabIcons: root.tabIcons
        activeTab: root.activeTab
        order: root.topTabOrder
        dropHoverIndex: assetDropArea.containsDrag && root.dragTabId.length > 0
                        && root.dragTabId !== root.currentTabId ? root.tabIndexOf(root.dragTabId) : -1
        onTabSelected: (index) => root.activeTab = index
    }

    Row {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.top: root.topTabs ? topTabBar.bottom : parent.top
        anchors.margins: root.sheetMode ? 0 : Theme.c.assetsPanel.borderWidth
        anchors.topMargin: root.topTabs ? 0 : (root.sheetMode ? 0 : Theme.c.assetsPanel.borderWidth)
        spacing: 0

        AssetsPanelTabRail {
            id: tabRail
            sheetMode: root.railHidden || root.topTabs
            railWidth: root.railWidth
            activeTab: root.activeTab
            tabCount: tabsModel.count
            tabLabels: root.tabLabels
            tabIcons: root.tabIcons
            tabs: tabsModel
            onTabSelected: (index) => root.activeTab = index
        }

        Rectangle {
            width: (root.railHidden || root.topTabs) ? 0 : Theme.c.assetsPanel.borderWidth
            height: parent.height
            visible: !root.railHidden && !root.topTabs
            color: Theme.c.assetsPanel.railBorder

            MouseArea {
                anchors.fill: parent
                anchors.leftMargin: -Theme.spacingXs
                anchors.rightMargin: -Theme.spacingXs
                cursorShape: Qt.SplitHCursor
                preventStealing: true
                onPositionChanged: (mouse) => {
                    if (!pressed)
                        return
                    root.railDragWidth = Math.max(Theme.tabRailWidth,
                        Math.min(root.maxRailWidth, mapToItem(tabRail, mouse.x, 0).x))
                }
                onReleased: {
                    if (root.railDragWidth < 0)
                        return
                    root.railUnits = root.railDragWidth / Theme.tabRailWidth
                    root.railDragWidth = -1
                    LayoutMemory.setLayoutNumber("assets/railUnits", root.railUnits)
                }
            }
        }

        Column {
            id: assetsContent
            width: parent.width - ((root.railHidden || root.topTabs) ? 0 : (tabRail.width + Theme.c.assetsPanel.borderWidth))
            height: parent.height

            AssetsPanelHeader {
                id: assetsHeader
                readonly property string tabId: tabsModel.get(root.activeTab).tabId
                readonly property bool hasActions: kindsForTab(tabId).length > 0
                readonly property bool searchBarTab: ["stock", "text", "stickers", "shapes", "effects", "templates",
                                                      "transitions", "sounds"].indexOf(tabId) >= 0
                width: parent.width
                height: (searchBarTab && !root.locked) || (root.sheetMode && !hasActions) ? 0 : Theme.panelHeaderHeight
                visible: height > 0
                title: tabLabels[tabId]
                showTitle: !root.sheetMode
                showActions: hasActions
                importing: root.importing
                onNewFolderRequested: newFolderDialog.open()
                onImportFilesRequested: root.importMedia()
                onImportFolderRequested: root.importFolder()
                onCollectMediaRequested: root.collectMedia()
            }

            EmojiPicker {
                id: emojiPicker
                // Hangs off the stickers search bar's + at the panel's right edge, and slides back
                // rather than running off-window when the panel is dragged narrow.
                x: Math.max((root.railHidden || root.topTabs) ? 0 : -tabRail.width, assetsContent.width - width - Theme.pagePadding)
                y: assetsHeader.height + Theme.pagePadding + Theme.controlHeight + 4
                onExtrasRequested: ShellBridge.main.openAddonManager("stickers")
                onAdded: root.addCompleted()
            }

            // Only the open tab is instantiated; what should outlive a switch is kept in
            // tabState below. Scroll position is not kept.
            Loader {
                id: titlesLoader
                active: root.currentTabId === "text"
                visible: active
                width: parent.width
                height: parent.height - assetsHeader.height
                clip: true
                opacity: root.tabOpacity
                // Titles: online animated title templates (Lottie, editable text) or the
                // built-in text styles.
                sourceComponent: Component {
                    OnlineSwitchTab {
                        tabId: "text"
                        onlineKinds: ["title"]
                        builtinLabel: qsTr("Text styles")
                        allowLinks: false
                        onAdded: root.addCompleted()
                        builtin: Component {
                            TextAssetsTab {
                                Component.onCompleted: {
                                    const saved = root.tabState["text"] || {}
                                    if (saved.section)
                                        section = saved.section
                                }
                                onSectionChanged: root.rememberTab("text", "section", section)
                                onAdded: root.addCompleted()
                            }
                        }
                    }
                }
            }

            Loader {
                active: root.currentTabId === "subtitles"
                visible: active
                width: parent.width
                height: parent.height - assetsHeader.height
                clip: true
                opacity: root.tabOpacity
                sourceComponent: Component {
                    SubtitlesTab {
                        onAdded: root.addCompleted()
                    }
                }
            }

            // Dafin: the AI category. The open tool is remembered across tab switches.
            Loader {
                active: root.currentTabId === "ai"
                visible: active
                width: parent.width
                height: parent.height - assetsHeader.height
                clip: true
                opacity: root.tabOpacity
                sourceComponent: Component {
                    AiToolsTab {
                        Component.onCompleted: openTool = (root.tabState["ai"] || {}).tool || ""
                        onOpenToolChanged: root.rememberTab("ai", "tool", openTool)
                    }
                }
            }

            Loader {
                active: root.currentTabId === "speech"
                visible: active
                width: parent.width
                height: parent.height - assetsHeader.height
                clip: true
                opacity: root.tabOpacity
                sourceComponent: Component {
                    SpeechTab { }
                }
            }

            Loader {
                active: root.currentTabId === "stock"
                visible: active
                width: parent.width
                height: parent.height - assetsHeader.height
                clip: true
                opacity: root.tabOpacity
                sourceComponent: Component {
                    OnlineAssetBrowser {
                        kinds: ["video"]
                        onAdded: root.addCompleted()
                    }
                }
            }

            Loader {
                id: soundsLoader
                active: root.currentTabId === "sounds"
                visible: active
                width: parent.width
                height: parent.height - assetsHeader.height
                clip: true
                opacity: root.tabOpacity
                // Audio: online music + sound effects, or the built-in audio-effect presets.
                sourceComponent: Component {
                    OnlineSwitchTab {
                        tabId: "sounds"
                        onlineKinds: ["music", "sfx"]
                        builtinLabel: qsTr("Audio effects")
                        onAdded: root.addCompleted()
                        builtin: Component {
                            SoundsTab {
                                Component.onCompleted: root.restoreBrowserTab(this, "sounds")
                                onSearchTextChanged: root.rememberTab("sounds", "search", searchText)
                                onActiveCategoryChanged: root.rememberTab("sounds", "category", activeCategory)
                            }
                        }
                    }
                }
            }

            Loader {
                id: stickersLoader
                active: root.currentTabId === "stickers"
                visible: active
                width: parent.width
                height: parent.height - assetsHeader.height
                clip: true
                opacity: root.tabOpacity
                // Stickers: online animated stickers (Lottie) or the built-in set.
                sourceComponent: Component {
                    OnlineSwitchTab {
                        tabId: "stickers"
                        onlineKinds: ["sticker"]
                        onAdded: root.addCompleted()
                        builtin: Component {
                            StickersTab {
                                emojiPickerOpen: emojiPicker.opened
                                onEmojiPickerRequested: emojiPicker.opened ? emojiPicker.close() : emojiPicker.open()
                                onAdded: root.addCompleted()
                                Component.onCompleted: root.restoreBrowserTab(this, "stickers")
                                onSearchTextChanged: root.rememberTab("stickers", "search", searchText)
                                onActiveCategoryChanged: root.rememberTab("stickers", "category", activeCategory)
                            }
                        }
                    }
                }
            }

            Loader {
                active: root.currentTabId === "shapes"
                visible: active
                width: parent.width
                height: parent.height - assetsHeader.height
                clip: true
                opacity: root.tabOpacity
                sourceComponent: Component {
                    ShapesTab {
                        onAdded: root.addCompleted()
                        Component.onCompleted: root.restoreBrowserTab(this, "shapes")
                        onSearchTextChanged: root.rememberTab("shapes", "search", searchText)
                        onActiveCategoryChanged: root.rememberTab("shapes", "category", activeCategory)
                    }
                }
            }

            Loader {
                active: root.currentTabId === "scenes"
                visible: active
                width: parent.width
                height: parent.height - assetsHeader.height
                clip: true
                opacity: root.tabOpacity
                sourceComponent: Component {
                    ScenesTab {
                        Component.onCompleted: {
                            const saved = root.tabState["scenes"] || {}
                            if (saved.sortByScore !== undefined)
                                sortByScore = saved.sortByScore
                            if (saved.labelFilter !== undefined)
                                labelFilter = saved.labelFilter
                        }
                        onSortByScoreChanged: root.rememberTab("scenes", "sortByScore", sortByScore)
                        onLabelFilterChanged: root.rememberTab("scenes", "labelFilter", labelFilter)
                    }
                }
            }

            Loader {
                active: root.currentTabId === "masks"
                visible: active
                width: parent.width
                height: parent.height - assetsHeader.height
                clip: true
                opacity: root.tabOpacity
                sourceComponent: Component {
                    MasksTab {
                        onAdded: root.addCompleted()
                    }
                }
            }

            Loader {
                active: root.currentTabId === "effects"
                visible: active
                width: parent.width
                height: parent.height - assetsHeader.height
                clip: true
                opacity: root.tabOpacity
                sourceComponent: Component {
                    EffectBrowser {
                        Component.onCompleted: root.restoreBrowserTab(this, "effects")
                        onSearchTextChanged: root.rememberTab("effects", "search", searchText)
                        onActiveCategoryChanged: root.rememberTab("effects", "category", activeCategory)
                    }
                }
            }

            Loader {
                active: root.currentTabId === "templates"
                visible: active
                width: parent.width
                height: parent.height - assetsHeader.height
                clip: true
                opacity: root.tabOpacity
                sourceComponent: Component {
                    EffectTemplateBrowser {
                        Component.onCompleted: root.restoreBrowserTab(this, "templates")
                        onSearchTextChanged: root.rememberTab("templates", "search", searchText)
                        onActiveCategoryChanged: root.rememberTab("templates", "category", activeCategory)
                    }
                }
            }

            Loader {
                active: root.currentTabId === "transitions"
                visible: active
                width: parent.width
                height: parent.height - assetsHeader.height
                clip: true
                opacity: root.tabOpacity
                sourceComponent: Component {
                    TransitionsTab {
                        Component.onCompleted: root.restoreBrowserTab(this, "transitions")
                        onSearchTextChanged: root.rememberTab("transitions", "search", searchText)
                        onActiveCategoryChanged: root.rememberTab("transitions", "category", activeCategory)
                    }
                }
            }

            // Shared media browser used by the Media tab.
            Loader {
                id: mediaBrowser
                active: root.kindsForTab(root.currentTabId).length > 0
                visible: active
                width: parent.width
                height: parent.height - assetsHeader.height
                clip: true
                opacity: root.tabOpacity
                sourceComponent: Component {
                    MediaAssetsTab {
                        importing: root.importing
                        assetVisibleFn: function(kind) { return root.assetVisible(kind) }
                        onPreviewRequested: (assetIndex) => {
                            if (ShellBridge.main && ShellBridge.main.openMediaPreview)
                                ShellBridge.main.openMediaPreview(assetIndex)
                        }
                        onAddToTimelineRequested: (assetIds) => root.requestAddToTimeline(assetIds)
                        onRemoveRequested: (assetIds) => root.requestRemoveAsset(assetIds)
                        onReplaceRequested: (assetIndex) => root.requestReplaceAsset(assetIndex)
                        onLinkProxyRequested: (assetId) => root.requestLinkProxy(assetId)
                        onRenameRequested: (assetIndex) => root.requestRenameAsset(assetIndex)
                        onExportRequested: (assetIndex) => root.requestExportAsset(assetIndex)
                        onSaveAsRequested: (assetIndex) => root.requestSaveAssetAs(assetIndex)
                        onImportRequested: root.importMedia()
                        onImportFolderRequested: root.importFolder()
                        onMoveToFolderRequested: (assetIds) => root.requestMoveAssetToFolder(assetIds)
                        onFolderRenameRequested: (folderId, folderName) => root.requestRenameFolder(folderId, folderName)
                        onFolderMoveRequested: (folderId) => root.requestMoveFolder(folderId)

                        Component.onCompleted: {
                            const saved = root.tabState["media"] || {}
                            if (saved.search)
                                searchText = saved.search
                            if (saved.sortByKind !== undefined)
                                sortByKind = saved.sortByKind
                            if (saved.expandedFolderIds)
                                expandedFolderIds = saved.expandedFolderIds
                            if (saved.selectedAssetIds)
                                selectedAssetIds = saved.selectedAssetIds
                            if (saved.selectionAnchorId)
                                selectionAnchorId = saved.selectionAnchorId
                            // Assets removed while the tab was unloaded must not come back selected.
                            pruneSelection()
                        }
                        onSearchTextChanged: root.rememberTab("media", "search", searchText)
                        onSortByKindChanged: root.rememberTab("media", "sortByKind", sortByKind)
                        onExpandedFolderIdsChanged: root.rememberTab("media", "expandedFolderIds", expandedFolderIds)
                        onSelectedAssetIdsChanged: root.rememberTab("media", "selectedAssetIds", selectedAssetIds)
                        onSelectionAnchorIdChanged: root.rememberTab("media", "selectionAnchorId", selectionAnchorId)
                    }
                }
            }
        }
    }
}
