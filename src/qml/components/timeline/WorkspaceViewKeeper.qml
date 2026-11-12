import QtQuick
import Drift

// Dafin: keeps the timeline panel's zoom and scroll per workspace. Before a switch it records
// them into the workspace being left; after it, it shows the new one as it was last seen.
QtObject {
    id: root

    // The TimelinePanel (its `zoom`, minZoom/maxZoom) and its Flickable.
    required property var panel
    required property var flickable

    readonly property Connections watcher: Connections {
        target: EditorState.workspaces
        function onAboutToSwitch() {
            EditorState.workspaces.rememberView(root.panel.zoom, root.flickable.contentX)
        }
        function onViewRestoreRequested(zoom, scrollX) {
            if (zoom > 0)
                root.panel.zoom = Math.max(root.panel.minZoom, Math.min(root.panel.maxZoom, zoom))
            // After the new timeline's width is laid out, or the scroll is clamped to the old one.
            Qt.callLater(() => {
                const maxX = Math.max(0, root.flickable.contentWidth - root.flickable.width)
                root.flickable.contentX = zoom > 0 ? Math.max(0, Math.min(maxX, scrollX)) : 0
            })
        }
    }
}
