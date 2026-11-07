import QtQuick
import Drift

// One toolbar cell: a hairline separator, or the button for one edit action.
Item {
    id: slot

    property var host
    // Long-press drag reordering of the toolbar's edit actions (set by TimelineToolbar).
    property BarReorder reorder
    required property string modelData
    required property int index
    readonly property bool isSeparator: modelData === "separator"

    readonly property string barItemId: host ? host.actionDragId(modelData, index) : modelData
    readonly property int barItemIndex: index
    readonly property string barGroup: "actions"

    visible: isSeparator
             ? host.separatorNeeded(host.layout.toolbar, index, host.actionShownOnToolbar)
             : host.actionShownOnToolbar(modelData)
    width: isSeparator ? Theme.c.timelineToolbarActionSlot.borderWidth : actionButton.width
    height: isSeparator ? Theme.spacing3xl : actionButton.height
    anchors.verticalCenter: parent ? parent.verticalCenter : undefined

    Rectangle {
        visible: slot.isSeparator
        anchors.fill: parent
        color: Theme.c.timelineToolbarActionSlot.foreground
    }

    TimelineToolbarToolButton {
        id: actionButton
        visible: !slot.isSeparator
        glyph: slot.isSeparator ? "" : slot.host.actionMeta[slot.modelData].glyph
        tooltip: slot.isSeparator ? "" : slot.host.actionTooltip(slot.modelData)
        active: slot.host.actionActive(slot.modelData)
        enabled: slot.host.actionEnabled(slot.modelData)
        onClicked: slot.host.triggerAction(slot.modelData)
    }

    BarReorderHandle {
        reorder: slot.reorder
        group: "actions"
        itemId: slot.barItemId
        content: actionButton
        draggable: slot.reorder !== null && !slot.isSeparator
    }
}
