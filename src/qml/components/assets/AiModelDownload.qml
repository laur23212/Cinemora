import QtQuick
import Drift

// Dafin: an AI tool's "download on first use" block. Says what has to be fetched (the AI engine
// too, when none is installed) and how big it is, then downloads it with a progress bar and a
// Cancel button. Models are add-ons: they land in the app data folder, and Settings › AI & Cloud
// › Downloaded AI models deletes them. The host hides this once the kinds it needs are there.
Column {
    id: root

    // Store ids to install, e.g. ["whisper.small", "silero.vad"]. The store adds what they need.
    property var addonIds: []
    // One sentence on what the download unlocks, shown above the button.
    property string purpose: ""
    property string buttonText: qsTr("Download")
    // A single line and a smaller button, for an optional extra under a tool that already works.
    property bool compact: false

    spacing: Theme.spacingSm

    // The ids this block started, plus anything of theirs already on its way when it opened.
    property var activeIds: []
    // id -> 0..1 for the current phase, from Addons.progressChanged.
    property var fractions: ({})

    // Re-read whenever the store says something changed: an index arrived, an install moved on.
    readonly property var plan: {
        void Addons.catalog
        return Addons.planInstall(root.addonIds)
    }
    readonly property var entries: {
        void Addons.catalog
        const out = {}
        const ids = root.addonIds.concat(root.activeIds)
        for (let i = 0; i < ids.length; ++i) {
            if (out[ids[i]] === undefined)
                out[ids[i]] = Addons.entry(ids[i])
        }
        for (let j = 0; j < (root.plan.ids || []).length; ++j) {
            const id = root.plan.ids[j]
            if (out[id] === undefined)
                out[id] = Addons.entry(id)
        }
        return out
    }

    function stateOf(id) {
        const entry = root.entries[id]
        return entry && entry.state ? entry.state : ""
    }
    function busyState(state) {
        return state === "queued" || state === "downloading" || state === "installing"
    }

    readonly property var busyIds: root.activeIds.filter(id => root.busyState(root.stateOf(id)))
    readonly property var failedIds: root.activeIds.filter(id => root.stateOf(id) === "failed")
    readonly property bool busy: root.busyIds.length > 0
    // The store's list never arrived (offline, or the server is down): nothing to offer yet.
    readonly property bool known: root.addonIds.some(id => !!root.entries[id] && !!root.entries[id].id)
    readonly property bool tooNew: root.addonIds.some(id => root.stateOf(id) === "needs-newer-app")

    // Weighted by size, so a 600 MB model is not half the bar next to a 2 MB one.
    readonly property real progress: {
        let total = 0
        let done = 0
        for (let i = 0; i < root.activeIds.length; ++i) {
            const id = root.activeIds[i]
            const entry = root.entries[id] || {}
            const weight = Math.max(1, Number(entry.downloadSize || 0))
            const state = root.stateOf(id)
            const f = Math.max(0, Math.min(1, Number(root.fractions[id] || 0)))
            total += weight
            if (state === "installed" || state === "update-available")
                done += weight
            else if (state === "downloading")
                done += weight * 0.9 * f
            else if (state === "installing")
                done += weight * (0.9 + 0.1 * f)
        }
        return total > 0 ? done / total : 0
    }

    readonly property string currentName: {
        for (let i = 0; i < root.busyIds.length; ++i) {
            const state = root.stateOf(root.busyIds[i])
            if (state === "downloading" || state === "installing")
                return (root.entries[root.busyIds[i]] || {}).name || ""
        }
        return ""
    }

    function start() {
        const ids = root.plan.ids || []
        if (ids.length === 0)
            return
        root.fractions = ({})
        root.activeIds = ids
        Addons.installMany(root.addonIds)
    }

    // Our add-ons and what they need (the AI engine), which may be queued from elsewhere too:
    // Extras, the Subtitles tab, or this tool before the panel was closed and opened again.
    function adoptQueued() {
        const related = root.addonIds.slice()
        for (let i = 0; i < root.addonIds.length; ++i) {
            const needs = (root.entries[root.addonIds[i]] || {}).requires || []
            for (let j = 0; j < needs.length; ++j) {
                if (needs[j].id && related.indexOf(needs[j].id) < 0)
                    related.push(needs[j].id)
            }
        }
        const queued = (Addons.queue.ids || []).filter(id => related.indexOf(id) >= 0)
        if (queued.length === 0)
            return
        const next = root.activeIds.slice()
        for (let k = 0; k < queued.length; ++k) {
            if (next.indexOf(queued[k]) < 0)
                next.push(queued[k])
        }
        if (next.length !== root.activeIds.length)
            root.activeIds = next
    }

    function cancel() {
        for (let i = 0; i < root.busyIds.length; ++i)
            Addons.cancel(root.busyIds[i])
    }

    function retry() {
        const ids = root.failedIds
        root.fractions = ({})
        Addons.installMany(ids)
    }

    Component.onCompleted: {
        root.adoptQueued()
        if (!root.known && !Addons.refreshing)
            Addons.refresh(false)
    }

    Connections {
        target: Addons
        function onQueueChanged() {
            root.adoptQueued()
        }
        function onProgressChanged(id, fraction, phase) {
            if (root.activeIds.indexOf(id) < 0)
                return
            const next = Object.assign({}, root.fractions)
            next[id] = fraction
            root.fractions = next
        }
    }

    Text {
        width: root.width
        visible: !root.busy && text.length > 0
        wrapMode: Text.WordWrap
        color: Theme.c.subtitlesTab.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        text: {
            if (root.tooNew)
                return qsTr("This needs a newer version of Dafin.")
            if (!root.known)
                return Addons.refreshing ? qsTr("Checking what can be downloaded…")
                                         : qsTr("Can't reach the download server. Check your internet connection and try again.")
            const ids = root.plan.ids || []
            if (ids.length === 0)
                return root.purpose
            const names = ids.map(id => (root.entries[id] || {}).name || id).join(", ")
            const size = DownloadFormat.formatBytes(root.plan.downloadSize || 0)
            if (size.length === 0)
                return qsTr("%1 One-time download: %2.").arg(root.purpose).arg(names)
            return root.compact ? qsTr("%1 Download: %2, about %3.").arg(root.purpose).arg(names).arg(size)
                                : qsTr("%1 One-time download, about %2: %3.").arg(root.purpose).arg(size).arg(names)
        }
    }

    Text {
        width: root.width
        visible: !root.busy && root.failedIds.length > 0
        wrapMode: Text.WordWrap
        color: Theme.c.scenesTab.warning
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        text: {
            if (root.failedIds.length === 0)
                return ""
            const entry = root.entries[root.failedIds[0]] || {}
            return entry.error ? qsTr("The download failed: %1").arg(entry.error)
                               : qsTr("The download failed.")
        }
    }

    ThemedButton {
        visible: !root.busy && !root.tooNew
        width: root.compact ? implicitWidth : root.width
        variant: root.compact ? "secondary" : "primary"
        glyph: root.known ? Theme.icons.download : Theme.icons.refresh
        text: !root.known ? qsTr("Try again")
                          : (root.failedIds.length > 0 ? qsTr("Try again") : root.buttonText)
        enabled: root.known ? (root.plan.ids || []).length > 0 || root.failedIds.length > 0
                            : !Addons.refreshing
        onClicked: {
            if (!root.known)
                Addons.refresh(true)
            else if (root.failedIds.length > 0)
                root.retry()
            else
                root.start()
        }
    }

    Column {
        width: root.width
        spacing: Theme.spacingSm
        visible: root.busy

        Text {
            width: parent.width
            elide: Text.ElideRight
            color: Theme.c.subtitlesTab.statusForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            text: root.currentName.length > 0
                  ? qsTr("Downloading %1… %2%").arg(root.currentName).arg(Math.round(root.progress * 100))
                  : qsTr("Waiting to download… %1%").arg(Math.round(root.progress * 100))
        }

        ThemedProgressBar {
            width: parent.width
            value: root.progress
        }

        ThemedButton {
            text: qsTr("Cancel")
            variant: "secondary"
            glyph: Theme.icons.x
            onClicked: root.cancel()
        }
    }
}
