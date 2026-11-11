import QtQuick
import Drift

// The projects under the start screen's current tab, as a grid of cards or a list of rows.
// Both grow to their full height; the start screen's Flickable scrolls the whole page.
Item {
    id: recents

    property var items: []
    property string view: "grid"
    property string emptyText: ""
    property string removeLabel: qsTr("Remove from list")

    signal openRecentRequested(string path)
    signal removeRequested(string path)
    signal revealRequested(string path)

    readonly property real gap: Theme.spacing2xl
    readonly property int columns: Math.max(1, Math.floor((width + gap) / (300 + gap)))
    readonly property real cardWidth: Math.floor((width + gap) / columns - gap)

    width: parent.width
    implicitHeight: items.length === 0 ? emptyLabel.implicitHeight + Theme.spacing2xl * 2
                    : view === "list" ? list.contentHeight
                    : grid.contentHeight

    // "Today, 14:28", "Yesterday, 19:10", "Oct 30, 16:42", and the year once it isn't this one.
    function friendlyDate(date) {
        if (!date || isNaN(date.getTime()) || date.getTime() <= 0)
            return ""
        const now = new Date()
        const today = new Date(now.getFullYear(), now.getMonth(), now.getDate())
        const day = new Date(date.getFullYear(), date.getMonth(), date.getDate())
        const daysAgo = Math.round((today - day) / 86400000)
        // Hours and minutes in the user's own style; some locales' short format carries seconds.
        const timeFormat = Qt.locale().timeFormat(Locale.ShortFormat).replace(/[:.]ss/, "")
        const time = date.toLocaleTimeString(Qt.locale(), timeFormat)
        if (daysAgo === 0)
            return qsTr("Today, %1").arg(time)
        if (daysAgo === 1)
            return qsTr("Yesterday, %1").arg(time)
        const format = date.getFullYear() === now.getFullYear() ? "MMM d" : "MMM d, yyyy"
        return qsTr("%1, %2").arg(date.toLocaleDateString(Qt.locale(), format)).arg(time)
    }

    // 02:17, or 1:02:17 past an hour; empty while the length isn't known.
    function clock(us) {
        const total = Math.round((us || 0) / 1000000)
        if (total <= 0)
            return ""
        const pad = (n) => (n < 10 ? "0" : "") + n
        const h = Math.floor(total / 3600)
        const m = Math.floor((total % 3600) / 60)
        const s = total % 60
        return h > 0 ? h + ":" + pad(m) + ":" + pad(s) : pad(m) + ":" + pad(s)
    }

    ThemedLabel {
        id: emptyLabel
        y: Theme.spacing2xl
        width: parent.width
        visible: recents.items.length === 0
        size: "base"
        horizontalAlignment: Text.AlignHCenter
        text: recents.emptyText
    }

    GridView {
        id: grid
        // One gap wider than the page, so every card is the same width and the last column still
        // lines up with the page's right edge.
        width: parent.width + recents.gap
        height: contentHeight
        visible: recents.view !== "list" && recents.items.length > 0
        interactive: false
        cellWidth: recents.cardWidth + recents.gap
        cellHeight: Math.round(recents.cardWidth * 9 / 16) + Theme.spacingMd + 48 + recents.gap
        model: visible ? recents.items : []
        delegate: StartScreenProjectCard {
            width: recents.cardWidth
            dateText: recents.friendlyDate(modelData.modified)
            durationText: recents.clock(modelData.durationUs)
            removeLabel: recents.removeLabel
            onOpenRequested: (path) => recents.openRecentRequested(path)
            onRemoveRequested: (path) => recents.removeRequested(path)
            onRevealRequested: (path) => recents.revealRequested(path)
        }
    }

    ListView {
        id: list
        width: parent.width
        // Non-interactive: the outer Flickable scrolls the whole page, so this grows to its full
        // content height rather than clipping and needing a second scroll gesture.
        height: contentHeight
        spacing: Theme.spacingSm
        visible: recents.view === "list" && recents.items.length > 0
        interactive: false
        model: visible ? recents.items : []
        delegate: StartScreenProjectRow {
            dateText: recents.friendlyDate(modelData.modified)
            durationText: recents.clock(modelData.durationUs)
            removeLabel: recents.removeLabel
            onOpenRequested: (path) => recents.openRecentRequested(path)
            onRemoveRequested: (path) => recents.removeRequested(path)
            onRevealRequested: (path) => recents.revealRequested(path)
        }
    }
}
