import QtQuick
import Drift

// Draws a bar's drag feedback: a live copy of the button under the pointer and an accent line
// where it will land (or a highlight on an extra target such as the More button). Place it over
// the whole bar, above the button rows, outside any clipping.
Item {
    id: layer

    required property BarReorder reorder

    anchors.fill: parent
    z: 1000

    readonly property var marker: {
        // Re-evaluated on every pointer move.
        reorder.dragScenePos
        reorder.targetIndex
        return reorder.markerSceneGeometry()
    }

    Rectangle {
        id: dropMarker
        visible: layer.marker !== null
        readonly property point local: layer.marker
                                       ? layer.mapFromItem(null, layer.marker.x, layer.marker.y)
                                       : Qt.point(0, 0)
        x: local.x - width / 2
        y: local.y + 2
        width: 2
        height: layer.marker ? Math.max(12, layer.marker.height - 4) : 0
        radius: 1
        color: Theme.accent
    }

    Rectangle {
        id: extraHighlight
        readonly property Item target: layer.reorder.targetExtra ? layer.reorder.targetExtra.item : null
        visible: target !== null
        readonly property point local: target ? target.mapToItem(layer, 0, 0) : Qt.point(0, 0)
        x: local.x - 2
        y: local.y - 2
        width: target ? target.width + 4 : 0
        height: target ? target.height + 4 : 0
        radius: 6
        color: "transparent"
        border.width: 2
        border.color: dropMarker.color
    }

    Image {
        id: ghost
        visible: layer.reorder.dragging && source != ""
        source: layer.reorder.ghostUrl
        cache: false
        smooth: true
        readonly property point local: layer.mapFromItem(null,
                                                         layer.reorder.dragScenePos.x - layer.reorder.grabOffset.x,
                                                         layer.reorder.dragScenePos.y - layer.reorder.grabOffset.y)
        x: local.x
        y: local.y
        width: layer.reorder.ghostSize.width
        height: layer.reorder.ghostSize.height
        opacity: 0.92
        scale: 1.06
    }
}
