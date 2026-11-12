pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin guided tour: dims the window, outlines the real UI element a step points at and shows the
// step's text in a bubble with Back, Next and Skip and a step counter.
//
// A step whose target is missing or hidden right now (a button the user moved into a menu, a
// panel that is not in this workspace) is skipped without a word; the counter counts only the
// steps that can be shown. Targets are looked up again as the tour runs, so a button moved
// mid-tour is still found.
Popup {
    id: root

    // The Tutorial feature being toured: { id, title, tour: [{ target, text }] }.
    property var feature: null
    // Index into feature.tour of the step shown, -1 before the first.
    property int pos: -1

    readonly property var steps: root.feature ? root.feature.tour : []

    // The highlighted rectangle in window coordinates, and the counter, refreshed by `refresh()`.
    property rect hole: Qt.rect(0, 0, 0, 0)
    property int counterIndex: 0
    property int counterTotal: 0
    property bool hasPrev: false
    property bool hasNext: false

    readonly property real outline: 6
    readonly property real bubbleWidth: Math.min(340, Math.max(220, root.width - 24))

    parent: Overlay.overlay
    x: 0
    y: 0
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    padding: 0
    modal: false
    dim: false
    focus: true
    closePolicy: Popup.NoAutoClose
    background: null

    // --- Tour logic ------------------------------------------------------------------------

    function _targetOf(i) {
        if (i < 0 || i >= root.steps.length)
            return null
        return Tutorial.findTarget(root.steps[i].target)
    }

    // The nearest step from `from` (inclusive) in direction `dir` that can be shown, or -1.
    function _seek(from, dir) {
        for (let i = from; i >= 0 && i < root.steps.length; i += dir) {
            if (root._targetOf(i))
                return i
        }
        return -1
    }

    // Starts the tour of `f`. False, with nothing opened, when none of its steps can be shown now.
    function startTour(f) {
        if (!f || !f.tour)
            return false
        root.feature = f
        const first = root._seek(0, 1)
        if (first < 0) {
            root.feature = null
            return false
        }
        root.pos = first
        root.refresh()
        if (!root.opened)
            root.open()
        return true
    }

    function next() {
        const n = root._seek(root.pos + 1, 1)
        if (n < 0) {
            root.close()
            return
        }
        root.pos = n
        root.refresh()
    }

    function back() {
        const p = root._seek(root.pos - 1, -1)
        if (p < 0)
            return
        root.pos = p
        root.refresh()
    }

    function skip() {
        root.close()
    }

    // Re-reads where the target is and which steps can be shown. Runs while the tour is open.
    function refresh() {
        if (!root.feature)
            return
        let target = root._targetOf(root.pos)
        if (!target) {
            // The element went away (panel hidden, button moved into a menu): carry on forward,
            // else back, else end the tour.
            let n = root._seek(root.pos + 1, 1)
            if (n < 0)
                n = root._seek(root.pos - 1, -1)
            if (n < 0) {
                root.close()
                return
            }
            root.pos = n
            target = root._targetOf(n)
            if (!target)
                return
        }

        let total = 0
        let index = 0
        for (let i = 0; i < root.steps.length; ++i) {
            if (!root._targetOf(i))
                continue
            ++total
            if (i <= root.pos)
                index = total
        }
        if (root.counterTotal !== total)
            root.counterTotal = total
        if (root.counterIndex !== index)
            root.counterIndex = index
        const prev = root._seek(root.pos - 1, -1) >= 0
        const nxt = root._seek(root.pos + 1, 1) >= 0
        if (root.hasPrev !== prev)
            root.hasPrev = prev
        if (root.hasNext !== nxt)
            root.hasNext = nxt

        const topLeft = target.mapToItem(null, 0, 0)
        const left = Math.max(0, topLeft.x)
        const top = Math.max(0, topLeft.y)
        const right = Math.min(root.width, topLeft.x + target.width)
        const bottom = Math.min(root.height, topLeft.y + target.height)
        const r = Qt.rect(left, top, Math.max(0, right - left), Math.max(0, bottom - top))
        if (r.x !== root.hole.x || r.y !== root.hole.y
                || r.width !== root.hole.width || r.height !== root.hole.height)
            root.hole = r
    }

    onOpened: {
        const host = Overlay.overlay ? Overlay.overlay.Window.window : null
        if (host && host.pushOverlayModal)
            host.pushOverlayModal()
        keyCatcher.forceActiveFocus()
        root.refresh()
    }

    onClosed: {
        const host = Overlay.overlay ? Overlay.overlay.Window.window : null
        if (host && host.popOverlayModal)
            host.popOverlayModal()
        root.feature = null
        root.pos = -1
    }

    Timer {
        interval: 60
        repeat: true
        running: root.opened
        onTriggered: root.refresh()
    }

    // --- Look ------------------------------------------------------------------------------

    readonly property color dimColor: Qt.rgba(0, 0, 0, 0.64)
    readonly property real hx: Math.max(0, root.hole.x - root.outline)
    readonly property real hy: Math.max(0, root.hole.y - root.outline)
    readonly property real hr: Math.min(root.width, root.hole.x + root.hole.width + root.outline)
    readonly property real hb: Math.min(root.height, root.hole.y + root.hole.height + root.outline)

    contentItem: FocusScope {
        id: keyCatcher
        focus: true

        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Escape) {
                root.skip()
            } else if (event.key === Qt.Key_Right || event.key === Qt.Key_Return
                       || event.key === Qt.Key_Enter) {
                root.next()
            } else if (event.key === Qt.Key_Left) {
                root.back()
            }
            event.accepted = true
        }

        // Four panels around the highlight; together they dim everything else.
        Rectangle { x: 0; y: 0; width: root.width; height: root.hy; color: root.dimColor }
        Rectangle { x: 0; y: root.hb; width: root.width; height: root.height - root.hb; color: root.dimColor }
        Rectangle { x: 0; y: root.hy; width: root.hx; height: root.hb - root.hy; color: root.dimColor }
        Rectangle { x: root.hr; y: root.hy; width: root.width - root.hr; height: root.hb - root.hy; color: root.dimColor }

        // The tour is a guide, not a way to use the app: clicks and scrolling stop here.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            hoverEnabled: true
            preventStealing: true
            onPressed: (mouse) => { mouse.accepted = true }
            onWheel: (wheel) => { wheel.accepted = true }
        }

        Rectangle {
            x: root.hx
            y: root.hy
            width: root.hr - root.hx
            height: root.hb - root.hy
            color: "transparent"
            radius: Theme.radiusMd
            border.color: Theme.primary
            border.width: 2
        }

        Rectangle {
            id: bubble
            width: root.bubbleWidth
            height: bubbleColumn.implicitHeight + Theme.spacingXl * 2
            radius: Theme.radiusLg
            color: Theme.c.panelFrame.background
            border.color: Theme.primary
            border.width: 1

            readonly property real margin: 12
            readonly property real gap: 12
            readonly property bool fitsBelow: root.hb + gap + height <= root.height - margin
            readonly property bool fitsAbove: root.hy - gap - height >= margin

            x: Math.round(Math.max(margin, Math.min(root.width - width - margin,
                                                    root.hole.x + root.hole.width / 2 - width / 2)))
            y: Math.round(fitsBelow ? root.hb + gap
                          : (fitsAbove ? root.hy - gap - height
                                       : Math.max(margin, root.height - height - margin)))

            Column {
                id: bubbleColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingXl
                spacing: Theme.spacingLg

                Text {
                    width: parent.width
                    text: root.pos >= 0 && root.pos < root.steps.length ? root.steps[root.pos].text : ""
                    wrapMode: Text.WordWrap
                    color: Theme.c.themedLabel.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeBase
                }

                Row {
                    width: parent.width
                    spacing: Theme.spacingMd

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - buttons.width - Theme.spacingMd
                        elide: Text.ElideRight
                        text: qsTr("Step %1 of %2").arg(root.counterIndex).arg(root.counterTotal)
                        color: Theme.c.themedLabel.foregroundMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                    }

                    Row {
                        id: buttons
                        spacing: Theme.spacingSm

                        ThemedButton {
                            text: qsTr("Skip")
                            variant: "ghost"
                            onClicked: root.skip()
                        }
                        ThemedButton {
                            text: qsTr("Back")
                            enabled: root.hasPrev
                            onClicked: root.back()
                        }
                        ThemedButton {
                            text: root.hasNext ? qsTr("Next") : qsTr("Done")
                            variant: "primary"
                            onClicked: root.next()
                        }
                    }
                }
            }
        }
    }
}
