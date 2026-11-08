pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin's Shortcuts menu, opened from the header button beside Agent: every action with its key,
// searchable, and each key can be changed right here. Click a key, press the new one (Esc cancels,
// Backspace clears). A changed key shows an arrow that puts the original back, and a key that
// another action already has can be moved over instead of only being refused.
Popup {
    id: root

    property string query: ""
    // {actionId, label, chord, holderId, holderLabel} while a refused key waits to be moved over.
    property var clash: null

    readonly property var rows: {
        const all = EditorState.actions || []
        const q = root.query.trim().toLowerCase()
        if (q.length === 0)
            return all
        return all.filter((action) => {
            const keys = action.shortcut || ""
            return action.label.toLowerCase().indexOf(q) >= 0
                   || action.id.toLowerCase().indexOf(q) >= 0
                   || keys.toLowerCase().indexOf(q) >= 0
                   || Theme.shortcutDisplay(keys).toLowerCase().indexOf(q) >= 0
        })
    }

    // The action a refused chord belongs to: setShortcut names it by label.
    function holderIdFor(holder) {
        const match = (EditorState.actions || []).find((action) => action.label === holder || action.id === holder)
        return match ? match.id : ""
    }

    function noteClash(actionId, label, chord, holder) {
        const holderId = root.holderIdFor(holder)
        root.clash = holderId !== "" && holderId !== actionId
                     ? { actionId: actionId, label: label, chord: chord, holderId: holderId, holderLabel: holder }
                     : null
        if (!root.clash)
            Toasts.warning(qsTr("“%1” is already used by %2.").arg(Theme.shortcutDisplay(chord)).arg(holder))
    }

    function moveClash() {
        const c = root.clash
        root.clash = null
        if (!c)
            return
        EditorState.setShortcut(c.holderId, "")
        const refused = EditorState.setShortcut(c.actionId, c.chord)
        if (refused.length > 0)
            Toasts.warning(qsTr("“%1” is already used by %2.").arg(Theme.shortcutDisplay(c.chord)).arg(refused))
        else
            Toasts.success(qsTr("%1 is now “%2”. %3 has no key now.")
                           .arg(Theme.shortcutDisplay(c.chord)).arg(c.label).arg(c.holderLabel))
    }

    function resetOne(actionId, label) {
        const original = EditorState.defaultShortcutFor(actionId)
        const refused = EditorState.setShortcut(actionId, original)
        if (refused.length > 0)
            root.noteClash(actionId, label, original, refused)
    }

    width: 480
    height: Math.min(600, Math.max(280, (root.parent && root.parent.Window.window
                                         ? root.parent.Window.window.height : 700) - 120))
    padding: 0
    modal: true
    dim: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    onOpened: {
        root.clash = null
        searchField.forceActiveFocus()
        searchField.selectAll()
    }
    onClosed: root.clash = null

    background: Rectangle {
        color: Theme.c.notificationsPopup.background
        border.width: Theme.c.notificationsPopup.borderWidth
        border.color: Theme.c.notificationsPopup.border
        radius: Theme.c.notificationsPopup.radius
    }

    // Opacity plus scale, matching the notifications popup next to it.
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: Theme.durationBase; easing.type: Theme.easing }
            NumberAnimation { property: "scale"; from: 0.96; to: 1.0; duration: Theme.durationBase; easing.type: Theme.easing }
        }
    }
    exit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: Theme.durationFast; easing.type: Theme.easing }
            NumberAnimation { property: "scale"; from: 1.0; to: 0.96; duration: Theme.durationFast; easing.type: Theme.easing }
        }
    }

    contentItem: Item {
        Column {
            id: head
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.spacingLg
            spacing: Theme.spacingMd

            Row {
                width: parent.width
                spacing: Theme.spacingMd

                IconGlyph {
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Theme.icons.keyboard
                    iconSize: Theme.iconSizeMd
                    iconColor: Theme.panelForeground
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Keyboard shortcuts")
                    color: Theme.panelForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeBase
                    font.weight: Font.Medium
                }
            }

            ThemedTextField {
                id: searchField
                width: parent.width
                placeholderText: qsTr("Search by action or key")
                onTextChanged: root.query = text
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Click a key, then press the new one. Esc cancels, Backspace clears it.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }

        Flickable {
            id: list
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: head.bottom
            anchors.bottom: foot.top
            anchors.topMargin: Theme.spacingMd
            anchors.bottomMargin: Theme.spacingSm
            contentHeight: rowsColumn.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar { }

            Column {
                id: rowsColumn
                width: list.width

                Repeater {
                    model: root.rows

                    Rectangle {
                        id: row
                        required property var modelData
                        required property int index
                        readonly property string original: EditorState.defaultShortcutFor(modelData.id)
                        readonly property bool changed: (modelData.shortcut || "") !== original

                        width: rowsColumn.width
                        height: 34
                        color: rowHover.hovered ? Theme.popoverHover : "transparent"

                        HoverHandler { id: rowHover }

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.spacingLg
                            anchors.right: resetButton.left
                            anchors.rightMargin: Theme.spacingSm
                            anchors.verticalCenter: parent.verticalCenter
                            text: row.modelData.label
                            elide: Text.ElideRight
                            color: Theme.panelForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }

                        IconButton {
                            id: resetButton
                            anchors.right: field.left
                            anchors.rightMargin: Theme.spacingXs
                            anchors.verticalCenter: parent.verticalCenter
                            visible: row.changed
                            glyph: Theme.icons.undo
                            buttonSize: Theme.iconButtonSize * 0.8
                            iconSize: Theme.iconSizeSm
                            tooltip: row.original.length > 0
                                     ? qsTr("Back to %1").arg(Theme.shortcutDisplay(row.original))
                                     : qsTr("Back to no key")
                            onClicked: root.resetOne(row.modelData.id, row.modelData.label)
                        }

                        ShortcutCaptureField {
                            id: field
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.spacingLg
                            anchors.verticalCenter: parent.verticalCenter
                            width: 150
                            actionId: row.modelData.id
                            shortcut: row.modelData.shortcut || ""
                            toastClash: false
                            onClashed: (chord, holder) => root.noteClash(row.modelData.id, row.modelData.label,
                                                                         chord, holder)
                        }
                    }
                }
            }
        }

        Text {
            anchors.centerIn: list
            visible: root.rows.length === 0
            text: qsTr("No shortcut matches “%1”").arg(root.query)
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        Column {
            id: foot
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.spacingLg
            spacing: Theme.spacingMd

            // A key another action has: move it over, or leave both as they are.
            Rectangle {
                visible: root.clash !== null
                width: parent.width
                height: visible ? clashRow.height + Theme.spacingMd * 2 : 0
                radius: Theme.radiusXs
                color: Theme.panelAccent

                Column {
                    id: clashRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: Theme.spacingMd
                    spacing: Theme.spacingSm

                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: root.clash
                              ? qsTr("“%1” is already the key for %2. Use it for %3 instead?")
                                .arg(Theme.shortcutDisplay(root.clash.chord)).arg(root.clash.holderLabel)
                                .arg(root.clash.label)
                              : ""
                        color: Theme.panelForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }

                    Row {
                        spacing: Theme.spacingSm

                        ThemedButton {
                            variant: "primary"
                            text: qsTr("Use it here")
                            onClicked: root.moveClash()
                        }
                        ThemedButton {
                            variant: "ghost"
                            text: qsTr("Keep as it was")
                            onClicked: root.clash = null
                        }
                    }
                }
            }

            Row {
                spacing: Theme.spacingSm

                ThemedButton {
                    variant: "secondary"
                    glyph: Theme.icons.undo
                    text: qsTr("Reset all")
                    tooltip: qsTr("Puts every shortcut back to its original key")
                    onClicked: {
                        root.clash = null
                        EditorState.resetShortcuts()
                        Toasts.success(qsTr("Shortcuts reset to defaults."))
                    }
                }
                ThemedButton {
                    variant: "ghost"
                    glyph: Theme.icons.settings
                    text: qsTr("Open in Settings")
                    onClicked: {
                        root.close()
                        ShellBridge.main.openSettings("keymap")
                    }
                }
            }
        }
    }
}
