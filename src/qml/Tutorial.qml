pragma Singleton
import QtQuick
import QtQuick.Window

// Dafin tutorial: the feature list and tour steps (resources/tutorial/tutorial.json, compiled into
// the app), and the registry that maps a tour id such as "bar.export" to the real UI element
// carrying it (see TourTarget). The text in the JSON is shown as written.
QtObject {
    id: root

    // [{ id, category, title, summary, tour: [{ target, text }] }] in file order.
    property var features: []
    // [{ name, items: [feature] }] in order of first appearance.
    property var categories: []
    property bool loaded: false
    property string loadError: ""

    function load() {
        if (root.loaded)
            return
        let text = ""
        try {
            const xhr = new XMLHttpRequest()
            xhr.open("GET", "qrc:/tutorial/tutorial.json", false)
            xhr.send()
            text = xhr.responseText
        } catch (e) {
            root.loadError = String(e)
            return
        }
        let doc = null
        try {
            doc = JSON.parse(text)
        } catch (e2) {
            root.loadError = String(e2)
            return
        }
        const list = []
        const raw = (doc && doc.features) ? doc.features : []
        for (let i = 0; i < raw.length; ++i) {
            const f = raw[i]
            if (!f || typeof f.id !== "string" || typeof f.title !== "string")
                continue
            list.push({
                id: f.id,
                category: f.category || "",
                title: f.title,
                summary: f.summary || "",
                tour: Array.isArray(f.tour) ? f.tour : []
            })
        }
        const groups = []
        const byName = {}
        for (let j = 0; j < list.length; ++j) {
            const name = list[j].category
            if (byName[name] === undefined) {
                byName[name] = groups.length
                groups.push({ name: name, items: [] })
            }
            groups[byName[name]].items.push(list[j])
        }
        root.features = list
        root.categories = groups
        root.loaded = true
    }

    function featureById(id) {
        root.load()
        for (let i = 0; i < root.features.length; ++i) {
            if (root.features[i].id === id)
                return root.features[i]
        }
        return null
    }

    // --- Guided-tour targets ----------------------------------------------------------------
    // id -> [Item]. More than one element can carry an id (a panel shown in two places); the
    // first one that is on screen wins.
    property var _targets: ({})

    function registerTarget(id, item) {
        if (!id || !item)
            return
        const list = root._targets[id] || []
        if (list.indexOf(item) < 0)
            list.push(item)
        root._targets[id] = list
    }

    function unregisterTarget(id, item) {
        const list = root._targets[id]
        if (!list)
            return
        const at = list.indexOf(item)
        if (at >= 0)
            list.splice(at, 1)
        if (list.length === 0)
            delete root._targets[id]
    }

    // An element a tour can point at: alive, shown, with a size, inside a window.
    function usable(item) {
        if (!item)
            return false
        try {
            return item.visible && item.width > 0 && item.height > 0
                   && item.Window.window !== null && item.Window.window !== undefined
        } catch (e) {
            return false
        }
    }

    // "a|b|c": the first of those ids that has a usable element. Null when none has — a missing
    // or hidden target is not an error, the step is skipped.
    function findTarget(spec) {
        const ids = String(spec || "").split("|")
        for (let i = 0; i < ids.length; ++i) {
            const list = root._targets[ids[i].trim()]
            if (!list)
                continue
            for (let j = 0; j < list.length; ++j) {
                if (root.usable(list[j]))
                    return list[j]
            }
        }
        return null
    }
}
