import QtQuick
import Drift

// Dafin: rulers along the top and left edge of the picture, in project pixels, while "Ruler" is on.
// The step follows the zoom so the numbers stay readable. Nothing here takes the pointer.
Item {
    id: root
    enabled: false

    readonly property real projectW: Math.max(1, EditorState.projectFile.projectWidth())
    readonly property real projectH: Math.max(1, EditorState.projectFile.projectHeight())
    // Screen pixels per project pixel.
    readonly property real scale: width / projectW
    readonly property real step: {
        const wanted = 70 / Math.max(0.0001, scale)
        const steps = [10, 20, 50, 100, 200, 250, 500, 1000, 2000, 5000]
        for (const s of steps) if (s >= wanted) return s
        return 10000
    }
    readonly property real thickness: 16

    Rectangle { width: parent.width; height: root.thickness; color: "#B3101418" }
    Rectangle { width: root.thickness; height: parent.height; color: "#B3101418" }

    Repeater {
        model: Math.ceil(root.projectW / root.step) + 1
        delegate: Item {
            required property int index
            x: index * root.step * root.scale
            height: root.thickness
            Rectangle { width: 1; height: parent.height * 0.7; anchors.bottom: parent.bottom; color: "#CCFFFFFF" }
            Text {
                x: 3; y: 0
                text: Math.round(index * root.step)
                color: "#CCFFFFFF"
                font.family: Theme.monoFontFamily
                font.pixelSize: 9
            }
        }
    }
    Repeater {
        model: Math.ceil(root.projectH / root.step) + 1
        delegate: Item {
            required property int index
            y: index * root.step * root.scale
            width: root.thickness
            Rectangle { height: 1; width: parent.width * 0.7; anchors.right: parent.right; color: "#CCFFFFFF" }
            Text {
                x: 1
                text: Math.round(index * root.step)
                color: "#CCFFFFFF"
                font.family: Theme.monoFontFamily
                font.pixelSize: 9
                rotation: -90
                transformOrigin: Item.TopLeft
                y: 2 + implicitWidth
            }
        }
    }
}
