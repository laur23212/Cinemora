pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin's Workspace menu, opened from the header button: every workspace in the project, the
// open one ticked. Click one to switch to it, × closes it (closeRequested; the button asks first
// when it has unsaved changes), and "New empty workspace" opens a fresh timeline.
Popup {
    id: root

    // The button confirms before closing a workspace with unsaved changes.
    signal closeRequested(string workspaceId, string name, bool unsaved)

    width: 340
    padding: 0
    modal: true
    dim: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: Theme.c.notificationsPopup.background
        border.width: Theme.c.notificationsPopup.borderWidth
        border.color: Theme.c.notificationsPopup.border
        radius: Theme.c.notificationsPopup.radius
    }

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

    contentItem: Column {
        spacing: 0
        topPadding: Theme.spacingLg
        bottomPadding: Theme.spacingMd

        Row {
            x: Theme.spacingLg
            spacing: Theme.spacingMd

            IconGlyph {
                anchors.verticalCenter: parent.verticalCenter
                glyph: Theme.icons.layers
                iconSize: Theme.iconSizeMd
                iconColor: Theme.panelForeground
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Workspaces")
                color: Theme.panelForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeBase
                font.weight: Font.Medium
            }
        }

        Text {
            x: Theme.spacingLg
            width: root.width - 2 * Theme.spacingLg
            topPadding: Theme.spacingSm
            bottomPadding: Theme.spacingMd
            wrapMode: Text.WordWrap
            text: qsTr("Each workspace has its own timeline and undo history. Media is shared.")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        Repeater {
            model: EditorState.workspaces.workspaces

            Rectangle {
                id: row
                required property var modelData
                readonly property bool isActive: modelData.active === true

                width: root.width
                height: 34
                color: rowArea.containsMouse ? Theme.popoverHover : "transparent"

                // Under the × button, which takes its own clicks.
                MouseArea {
                    id: rowArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.close()
                        if (!row.isActive)
                            EditorState.workspaces.switchTo(row.modelData.id)
                    }
                }

                IconGlyph {
                    id: tick
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingLg
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Theme.icons.check
                    iconSize: Theme.iconSizeSm
                    iconColor: Theme.accent
                    opacity: row.isActive ? 1 : 0
                }

                IconGlyph {
                    id: kindIcon
                    anchors.left: tick.right
                    anchors.leftMargin: Theme.spacingSm
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: row.modelData.render ? Theme.icons.film : Theme.icons.layers
                    iconSize: Theme.iconSizeSm
                    iconColor: Theme.mutedForeground
                }

                Text {
                    anchors.left: kindIcon.right
                    anchors.leftMargin: Theme.spacingSm
                    anchors.right: unsavedDot.left
                    anchors.rightMargin: Theme.spacingSm
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.name
                    elide: Text.ElideRight
                    color: Theme.panelForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                    font.weight: row.isActive ? Font.DemiBold : Font.Normal
                }

                Rectangle {
                    id: unsavedDot
                    anchors.right: closeButton.left
                    anchors.rightMargin: Theme.spacingSm
                    anchors.verticalCenter: parent.verticalCenter
                    width: 7
                    height: 7
                    radius: 3.5
                    color: Theme.warning
                    visible: row.modelData.unsaved === true
                    HoverHandler { id: dotHover }
                    ThemedToolTip {
                        visible: dotHover.hovered
                        text: qsTr("Unsaved changes")
                    }
                }

                IconButton {
                    id: closeButton
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingSm
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Theme.icons.x
                    variant: "ghost"
                    iconSize: Theme.iconSizeSm
                    // At least one workspace always stays open.
                    visible: EditorState.workspaces.count > 1
                    tooltip: qsTr("Close this workspace (a render queued from it keeps going)")
                    onClicked: {
                        root.close()
                        root.closeRequested(row.modelData.id, row.modelData.name,
                                            row.modelData.unsaved === true)
                    }
                }
            }
        }

        Item { width: 1; height: Theme.spacingSm }

        ThemedButton {
            x: Theme.spacingLg
            width: root.width - 2 * Theme.spacingLg
            variant: "secondary"
            glyph: Theme.icons.plus
            text: qsTr("New empty workspace")
            onClicked: {
                root.close()
                EditorState.workspaces.addEmpty()
            }
        }
    }
}
