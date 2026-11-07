import QtQuick

// One rearrangeable place in a button bar: loads the item `items[modelData]` names and makes it
// long-press draggable. Use it as the delegate of a Repeater over `reorder.layout[group]`.
//
// An item that hides itself (a badge that only shows during an export) declares
// `readonly property bool barVisible` and binds its own `visible` to it, so the slot hides with
// it rather than leaving a gap. Reading the item's `visible` instead would never come back true:
// a hidden slot makes its child report hidden too.
Loader {
    id: slot

    required property string modelData
    required property int index
    property BarReorder reorder
    property string group
    // id -> Component
    property var items: ({})

    readonly property string barItemId: modelData
    readonly property int barItemIndex: index
    readonly property string barGroup: group

    sourceComponent: items[modelData] !== undefined ? items[modelData] : null
    active: sourceComponent !== null
    visible: item !== null && (item.barVisible === undefined || item.barVisible)
    anchors.verticalCenter: parent ? parent.verticalCenter : undefined

    BarReorderHandle {
        reorder: slot.reorder
        group: slot.group
        itemId: slot.modelData
        content: slot.item
        draggable: slot.item !== null
    }
}
