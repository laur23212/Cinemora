import QtQuick
import QtQuick.Controls.Basic
import Drift

SplitView {
    id: root

    property string workspaceId: ""
    property string windowId: "main"
    property var path: []

    anchors.fill: parent
    spacing: 0

    handle: PanelSplitHandle { view: root; onResetRequested: Qt.callLater(root.resetSizes) }

    // Written once a drag ends, as fractions of the split's extent. Persisting is silent
    // (no rebuild): the layout on screen already matches.
    onResizingChanged: {
        if (root.resizing || (ShellBridge.main?.previewFullscreen ?? false))
            return
        const horizontal = root.orientation === Qt.Horizontal
        const extent = horizontal ? root.width : root.height
        if (extent <= 0)
            return
        const sizes = []
        for (let i = 0; i < root.count; ++i) {
            const cell = root.itemAt(i)
            cell.fraction = (horizontal ? cell.width : cell.height) / extent
            cell.rebind()
            sizes.push(cell.fraction)
        }
        Workspaces.setSizes(root.workspaceId, root.windowId, root.path, sizes)

        // Dafin: a divider released within 2 % of a useful size (1/4, 1/3, 1/2, 2/3, 3/4)
        // settles on it.
        const snapped = Workspaces.snapSizes(sizes)
        let moved = false
        for (let i = 0; i < snapped.length; ++i)
            moved = moved || Math.abs(snapped[i] - sizes[i]) > 0.0005
        if (moved)
            root.applySizes(snapped)
    }

    // Dafin: gives every cell its fraction, then remembers them.
    function applySizes(fractions) {
        if (fractions.length !== root.count)
            return
        for (let i = 0; i < root.count; ++i) {
            const cell = root.itemAt(i)
            cell.fraction = fractions[i]
            cell.rebind()
        }
        Workspaces.setSizes(root.workspaceId, root.windowId, root.path, fractions)
    }

    // Dafin: double-click on a divider puts this split back to the built-in sizes (equal
    // sections for a workspace that is not a built-in).
    function resetSizes() {
        if (root.count < 2 || (ShellBridge.main?.previewFullscreen ?? false))
            return
        let fractions = Workspaces.presetSizes(root.workspaceId, root.windowId, root.path)
        if (fractions.length !== root.count) {
            fractions = []
            for (let i = 0; i < root.count; ++i)
                fractions.push(1 / root.count)
        }
        root.applySizes(fractions)
    }
}
