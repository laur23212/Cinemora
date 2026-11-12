import QtQuick
import Drift

// Dafin: a MouseArea for custom-drawn clickable things (cards, tiles, "add" slots) that gives
// them what ThemedButton and IconButton already have: a hover tint, a pressed tint, a keyboard
// focus ring (Tab to reach it, Enter or Space to click) and a pointing-hand cursor. All colours
// are shared theme tokens and the fades follow Theme.durationFast, so "reduce motion" applies.
//
// Put it where you would put the MouseArea and keep the same onClicked:
//     ClickableArea { anchors.fill: parent; radius: 6; onClicked: doIt() }
// The tint and ring are drawn above the siblings under it; the area itself stays transparent.
MouseArea {
    id: root

    property real radius: Theme.radiusMd
    // Turn off for a thing whose own look already shows hover (the tint would double up).
    property bool showHover: true
    property bool showPressed: true

    hoverEnabled: true
    activeFocusOnTab: enabled
    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    Accessible.role: Accessible.Button

    // A MouseArea only takes focus by Tab (a click does not give it focus), so activeFocus
    // means keyboard focus and the ring is shown for exactly that.
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
            root.clicked(null)
            event.accepted = true
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: root.pressed && root.showPressed ? Theme.c.themedButton.ghostBackgroundPressed
               : root.containsMouse && root.showHover ? Theme.c.themedButton.ghostBackgroundHover
               : "transparent"
        Behavior on color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: "transparent"
        border.width: Theme.borderWidthFocus
        border.color: Theme.focusRing
        visible: root.activeFocus
    }
}
