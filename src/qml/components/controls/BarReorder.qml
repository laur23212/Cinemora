import QtQuick

// Long-press-and-drag reordering for one button bar (the header, the preview transport, the
// timeline toolbar). A bar is split into named groups — the rows its items sit in, e.g. the
// header's left and right clusters — and a button can be dropped anywhere in its own bar: in
// its group or in another group of the same pool. Groups in different pools never exchange
// items (the timeline's edit actions are stored with its More menu, so they keep to themselves).
//
// The bar renders each group with a Repeater over `layout[group]`, puts a BarReorderHandle on
// every item, calls registerGroup() for each Row and gives the controller a BarReorderLayer to
// draw the drag ghost and the drop marker in. The order is kept by whoever handles saveRequested.
QtObject {
    id: reorder

    // group -> [item ids] as shipped. Also the full catalogue: ids not listed here are dropped
    // from a stored layout, and ids missing from it (new in this version) go back to their
    // default group, at the end.
    property var defaults: ({})
    // group -> pool name; groups left out are in pool "main".
    property var pools: ({})
    // The stored arrangement (group -> [ids]), or undefined for the defaults.
    property var stored
    // The layer the ghost and drop marker are drawn in; it must cover the whole bar.
    property Item layer
    // Extra drop targets outside the groups, e.g. the timeline's More button:
    // [{ item: Item, groups: [group names it accepts from], drop: function(id, fromGroup) }].
    property var extraTargets: []

    // Saves `layout` (group -> [ids]); the bar writes it to its preference.
    signal saveRequested(var layout)

    readonly property var layout: clean(stored)

    // Drag state, read by the handles and the layer.
    property string dragId: ""
    property string dragGroup: ""
    property Item dragSource: null
    property point dragScenePos: Qt.point(0, 0)
    property point grabOffset: Qt.point(0, 0)
    // Snapshot of the item taken as the drag starts, drawn under the pointer.
    property url ghostUrl
    property size ghostSize
    property string targetGroup: ""
    property int targetIndex: -1
    property var targetExtra: null
    readonly property bool dragging: dragId !== ""

    // group -> Row (or any positioner) holding that group's items in model order.
    property var _views: ({})

    function poolOf(group) {
        return pools[group] !== undefined ? pools[group] : "main"
    }

    function clean(saved) {
        const result = {}
        const seen = {}
        const known = {}
        for (const g in defaults) {
            for (const id of defaults[g])
                known[id] = g
        }
        for (const g in defaults) {
            result[g] = []
            const list = saved && saved[g] ? saved[g] : (saved ? [] : defaults[g])
            for (const raw of list) {
                const id = String(raw)
                const home = known[id]
                if (home === undefined || seen[id] || poolOf(home) !== poolOf(g))
                    continue
                seen[id] = true
                result[g].push(id)
            }
        }
        for (const g in defaults) {
            for (const id of defaults[g]) {
                if (!seen[id]) {
                    seen[id] = true
                    result[g].push(id)
                }
            }
        }
        return result
    }

    function registerGroup(group, view) {
        const views = Object.assign({}, _views)
        views[group] = view
        _views = views
    }

    // Slots expose barItemId, barItemIndex and barGroup.
    // The positioner's children that are items of `group`, in model order (a Repeater adds its
    // delegates as children, but not necessarily in order, so they are sorted by index).
    function _slots(group) {
        const view = _views[group]
        if (!view)
            return []
        const out = []
        for (let i = 0; i < view.children.length; ++i) {
            const child = view.children[i]
            if (child.barItemId !== undefined && child.barItemIndex !== undefined && child.barGroup === group)
                out.push(child)
        }
        out.sort((a, b) => a.barItemIndex - b.barItemIndex)
        return out
    }

    function _sceneRect(item) {
        const p = item.mapToItem(null, 0, 0)
        return Qt.rect(p.x, p.y, item.width, item.height)
    }

    function begin(id, group, source, scenePos, ghost) {
        const r = _sceneRect(source)
        ghostUrl = ghost ? ghost : ""
        ghostSize = Qt.size(r.width, r.height)
        dragSource = source
        grabOffset = Qt.point(scenePos.x - r.x, scenePos.y - r.y)
        dragGroup = group
        dragId = id
        move(scenePos)
    }

    function move(scenePos) {
        if (!dragging)
            return
        dragScenePos = scenePos
        for (const target of extraTargets) {
            if (target.item && target.item.visible && target.groups.indexOf(dragGroup) >= 0) {
                const r = _sceneRect(target.item)
                if (scenePos.x >= r.x && scenePos.x <= r.x + r.width) {
                    targetExtra = target
                    targetGroup = ""
                    targetIndex = -1
                    return
                }
            }
        }
        targetExtra = null
        // The group under the pointer, or the nearest one along the bar.
        let best = ""
        let bestDistance = Infinity
        for (const g in _views) {
            const view = _views[g]
            if (!view || !view.visible || poolOf(g) !== poolOf(dragGroup))
                continue
            const r = _sceneRect(view)
            const distance = scenePos.x < r.x ? r.x - scenePos.x
                           : scenePos.x > r.x + r.width ? scenePos.x - (r.x + r.width) : 0
            if (distance < bestDistance) {
                bestDistance = distance
                best = g
            }
        }
        targetGroup = best
        targetIndex = best === "" ? -1 : _indexAt(best, scenePos.x)
    }

    // Model index in `group` the dragged item would be inserted before.
    function _indexAt(group, sceneX) {
        const view = _views[group]
        const mirrored = view.effectiveLayoutDirection === Qt.RightToLeft
        const slots = _slots(group)
        for (const slot of slots) {
            if (!slot.visible || slot.width <= 0)
                continue
            const r = _sceneRect(slot)
            const centre = r.x + r.width / 2
            if (mirrored ? sceneX > centre : sceneX < centre)
                return slot.barItemIndex
        }
        return layout[group] ? layout[group].length : 0
    }

    // Where the drop marker goes, in scene coordinates: {x, y, height}, or null.
    function markerSceneGeometry() {
        if (!dragging || targetGroup === "" || targetIndex < 0)
            return null
        const view = _views[targetGroup]
        if (!view)
            return null
        const mirrored = view.effectiveLayoutDirection === Qt.RightToLeft
        const vr = _sceneRect(view)
        const slots = _slots(targetGroup).filter((s) => s.visible && s.width > 0)
        let x = mirrored ? vr.x + vr.width : vr.x
        let before = null
        let after = null
        for (const slot of slots) {
            if (slot.barItemIndex >= targetIndex) {
                before = slot
                break
            }
            after = slot
        }
        if (before) {
            const r = _sceneRect(before)
            x = mirrored ? r.x + r.width + 1 : r.x - 1
            if (after) {
                const a = _sceneRect(after)
                x = mirrored ? (a.x + r.x + r.width) / 2 : (a.x + a.width + r.x) / 2
            }
        } else if (after) {
            const a = _sceneRect(after)
            x = mirrored ? a.x - 1 : a.x + a.width + 1
        }
        return { x: x, y: vr.y, height: vr.height }
    }

    function end(commit) {
        if (!dragging)
            return
        const id = dragId
        const from = dragGroup
        const extra = targetExtra
        const to = targetGroup
        let at = targetIndex
        dragId = ""
        dragGroup = ""
        dragSource = null
        ghostUrl = ""
        targetExtra = null
        targetGroup = ""
        targetIndex = -1
        if (!commit)
            return
        // Deferred: saving rebuilds the bar's Repeaters, which would destroy the handle whose
        // pointer event is still being delivered.
        if (extra) {
            Qt.callLater(() => extra.drop(id, from))
            return
        }
        if (to === "" || at < 0)
            return
        const next = {}
        for (const g in layout)
            next[g] = layout[g].slice()
        const fromIndex = next[from].indexOf(id)
        if (fromIndex < 0)
            return
        next[from].splice(fromIndex, 1)
        if (to === from && fromIndex < at)
            at -= 1
        if (to === from && at === fromIndex)
            return
        next[to].splice(Math.min(at, next[to].length), 0, id)
        Qt.callLater(() => reorder.saveRequested(next))
    }

    // Puts `id` at `index` of `group` (used by tests and keyboard alternatives).
    function moveTo(id, group, index) {
        const next = {}
        let from = ""
        for (const g in layout) {
            next[g] = layout[g].slice()
            if (next[g].indexOf(id) >= 0)
                from = g
        }
        if (from === "" || next[group] === undefined || poolOf(from) !== poolOf(group))
            return
        const fromIndex = next[from].indexOf(id)
        next[from].splice(fromIndex, 1)
        if (from === group && fromIndex < index)
            index -= 1
        next[group].splice(Math.max(0, Math.min(index, next[group].length)), 0, id)
        saveRequested(next)
    }
}
