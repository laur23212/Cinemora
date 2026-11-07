import QtQuick
import QtQuick.Controls.Basic
import QtTest
import Drift

// Long-press drag reordering of bar buttons (src/qml/components/controls/BarReorder*.qml).
// The app's QML module cannot be imported by a test process, so CMake copies the four reorder
// files into a small "Drift" module beside a stub Theme.
Item {
    id: win; width: 600; height: 60
    property var saved: null
    property string droppedOnMore: ""
    property var clicks: ({})
    function click(id) { const c = Object.assign({}, clicks); c[id] = (c[id] || 0) + 1; clicks = c }

    BarReorder {
        id: ctl
        defaults: ({ "left": ["a", "b", "c"], "right": ["d", "e"] })
        stored: win.saved
        layer: layer
        extraTargets: [{ item: more, groups: ["left"], drop: (id, from) => win.droppedOnMore = id }]
        onSaveRequested: (l) => win.saved = l
    }
    readonly property var items: ({ "a": comp, "b": comp, "c": comp, "d": comp, "e": comp })
    Component { id: comp; Button { width: 40; height: 30; onClicked: win.click(parent.barItemId) } }

    Row {
        id: left; x: 10; height: 60; spacing: 4
        Component.onCompleted: ctl.registerGroup("left", left)
        Repeater { model: ctl.layout["left"]; delegate: BarSlot { reorder: ctl; group: "left"; items: win.items } }
    }
    Row {
        id: right; x: 400; height: 60; spacing: 4
        Component.onCompleted: ctl.registerGroup("right", right)
        Repeater { model: ctl.layout["right"]; delegate: BarSlot { reorder: ctl; group: "right"; items: win.items } }
    }
    // Stands in for the timeline's More button: dropping an action here moves it to the menu.
    Rectangle { id: more; x: 250; y: 15; width: 40; height: 30 }

    BarReorderLayer { id: layer; reorder: ctl }

    function slotFor(row, id) {
        for (let i = 0; i < row.children.length; ++i)
            if (row.children[i].barItemId === id) return row.children[i]
        return null
    }

    TestCase {
        name: "BarReorder"; when: windowShown
        // Resetting the layout rebuilds the Repeaters; the old delegates go away a moment later.
        function init() { win.saved = null; win.droppedOnMore = ""; win.clicks = ({}); wait(100) }

        function test_clean() {
            compare(ctl.clean(undefined), { left: ["a","b","c"], right: ["d","e"] })
            // unknown dropped, duplicates dropped, cross-group kept, missing appended to home group
            compare(ctl.clean({ left: ["c", "zz", "d", "c"], right: ["a"] }),
                    { left: ["c", "d", "b"], right: ["a", "e"] })
        }

        function test_clickStillWorks() {
            const a = slotFor(left, "a")
            mouseClick(a.item)
            compare(win.clicks["a"], 1)
            compare(win.saved, null)
        }

        function test_dragWithinGroup() {
            const a = slotFor(left, "a"); const c = slotFor(left, "c")
            mousePress(a.item, 20, 15)
            tryVerify(() => ctl.dragging, 2000)
            const p = c.mapToItem(win, c.width - 4, 15)
            mouseMove(win, p.x - 30, p.y); mouseMove(win, p.x, p.y)
            mouseRelease(win, p.x, p.y)
            tryVerify(() => win.saved !== null)
            compare(win.saved.left, ["b", "c", "a"])
            compare(win.clicks["a"], undefined)
            verify(!ctl.dragging)
        }

        function test_dragToOtherGroup() {
            const b = slotFor(left, "b"); const d = slotFor(right, "d")
            mousePress(b.item, 20, 15)
            tryVerify(() => ctl.dragging, 2000)
            const p = d.mapToItem(win, 2, 15)
            mouseMove(win, p.x - 100, p.y); mouseMove(win, p.x, p.y)
            mouseRelease(win, p.x, p.y)
            tryVerify(() => win.saved !== null)
            compare(win.saved.left, ["a", "c"])
            compare(win.saved.right, ["b", "d", "e"])
        }

        function test_shortPressMoveIsNotADrag() {
            const a = slotFor(left, "a")
            mousePress(a.item, 20, 15)
            mouseMove(a.item, 40, 15); mouseMove(a.item, 60, 15)
            wait(700)
            verify(!ctl.dragging)
            mouseRelease(a.item, 60, 15)
            compare(win.saved, null)
        }

        function test_escapeCancels() {
            const a = slotFor(left, "a")
            mousePress(a.item, 20, 15)
            tryVerify(() => ctl.dragging, 2000)
            mouseMove(win, 300, 15)
            keyClick(Qt.Key_Escape)
            verify(!ctl.dragging)
            mouseRelease(win, 300, 15)
            wait(50)
            compare(win.saved, null)
        }
    
        function test_dropOnExtraTarget() {
            const b = slotFor(left, "b")
            mousePress(b.item, 20, 15)
            tryVerify(() => ctl.dragging, 2000)
            mouseMove(win, 200, 30); mouseMove(win, 270, 30)
            compare(ctl.targetExtra !== null, true)
            mouseRelease(win, 270, 30)
            tryCompare(win, "droppedOnMore", "b")
            compare(win.saved, null)
        }
    }
}
