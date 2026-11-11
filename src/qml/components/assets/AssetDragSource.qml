import QtQuick
import Drift
// Makes whatever it covers draggable as an asset. The gesture follows the device of each press, not
// the form factor: a mouse/touchpad/stylus clicks for the card's primary action (tapped), drags as a
// platform drag (the mime type comes from AssetDrag) and right-clicks for contextMenuRequested; a
// finger presses-and-holds to lift into TouchDrag and taps for contextMenuRequested when the card
// has a menu (hasContextMenu), else for tapped.
//
// The Drag attached properties sit on this item and the handlers on a child: a handler on the item
// that owns Drag.Automatic re-enters QDrag::exec through its own active binding.
Item {
    id: root

    required property string kind
    required property var payload
    property string label: ""
    property string thumbnail: ""
    property string glyph: ""
    property alias liftEnabled: lift.liftEnabled
    // Dafin: when set, the card drags as that file (text/uri-list), exactly like a file dragged
    // in from the file manager: the timeline and the preview import it and place it where it
    // lands. Used by Lottie stickers and titles, which are files rather than catalog ids.
    property url fileUrl: ""
    readonly property bool fileDrag: fileUrl.toString().length > 0

    readonly property bool active: mouseDrag.active || lift.lifted
    readonly property bool pressed: tap.pressed || lift.pressed
    readonly property bool hovered: hover.hovered

    // Whether a finger's tap should open the card's menu instead of running its primary action.
    property bool hasContextMenu: false

    signal tapped()
    signal doubleTapped()
    signal contextMenuRequested()

    Drag.active: mouseDrag.active
    Drag.dragType: Drag.Automatic
    Drag.supportedActions: Qt.CopyAction
    Drag.keys: root.fileDrag ? ["text/uri-list"] : [AssetDrag.mimeKey(root.kind)]
    Drag.mimeData: root.fileDrag ? ({ "text/uri-list": root.fileUrl.toString() })
                                 : AssetDrag.mimeData(root.kind, root.payload, root.label)
    // Centred, so the drop lands under the pointer rather than a card's width to its left.
    Drag.hotSpot.x: width / 2
    Drag.hotSpot.y: height / 2

    Item {
        anchors.fill: parent

        HoverHandler {
            id: hover
            cursorShape: mouseDrag.active ? Qt.ClosedHandCursor : Qt.PointingHandCursor
        }

        TapHandler {
            id: tap
            acceptedButtons: Qt.LeftButton
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
            onTapped: root.tapped()
            onDoubleTapped: root.doubleTapped()
        }

        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: root.contextMenuRequested()
        }

        DragHandler {
            id: mouseDrag
            target: null
            acceptedButtons: Qt.LeftButton
            // Not the touchscreen: a finger on a desktop touchscreen scrolls the grid.
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
            onActiveChanged: {
                // A file drag is marked "file" so the assets panel knows it came from inside
                // and does not file it into My Lotties again.
                if (active)
                    AssetDrag.begin(root.fileDrag ? "file" : root.kind, root.fileDrag ? root.fileUrl : root.payload, root.label)
                else
                    AssetDrag.end()
            }
        }

        TouchLiftArea {
            id: lift
            dragKind: root.kind
            payload: root.payload
            label: root.label
            thumbnail: root.thumbnail
            glyph: root.glyph
            onLiftTapped: root.hasContextMenu ? root.contextMenuRequested() : root.tapped()
        }
    }
}
