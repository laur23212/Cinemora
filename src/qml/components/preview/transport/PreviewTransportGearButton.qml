import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift

// Dafin: the preview's gear menu, as in Filmora. Change the project's aspect ratio, draw from the
// original media while paused, zoom, safe zones, ruler and guides. `panel` is the PreviewPanel,
// which owns the safe-zone and ruler overlays and the viewport the zoom acts on.
IconButton {
    id: button

    property var panel

    Layout.alignment: Qt.AlignVCenter
    glyph: Theme.icons.settings
    variant: "text"
    tooltip: qsTr("Preview settings")
    active: menu.visible
    onClicked: menu.opened ? menu.close() : menu.open()

    // One row of the menu: a tick column, the label, and an optional shortcut on the right.
    component MenuRow: Rectangle {
        id: row
        property string text
        property string shortcut: ""
        property bool checked: false
        property bool showCheck: false
        property bool hasArrow: false
        signal triggered()

        width: 270
        height: Theme.controlHeight
        radius: Theme.c.previewOptionsMenu.radius
        opacity: enabled ? 1 : 0.4
        color: rowMouse.containsMouse && row.enabled ? Theme.c.iconButton.textBackgroundHover : "transparent"

        IconGlyph {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingMd
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.check
            iconSize: Theme.iconSizeSm
            iconColor: Theme.c.iconButton.foregroundActive
            visible: row.showCheck && row.checked
        }
        ThemedLabel {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingMd * 2 + Theme.iconSizeSm
            anchors.verticalCenter: parent.verticalCenter
            text: row.text
            size: "sm"
        }
        ThemedLabel {
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingMd
            anchors.verticalCenter: parent.verticalCenter
            text: row.hasArrow ? (zoomList.visible ? "▾" : "▸") : row.shortcut
            size: "xs"
            tone: "muted"
        }
        MouseArea {
            id: rowMouse
            anchors.fill: parent
            hoverEnabled: true
            enabled: row.enabled
            onClicked: row.triggered()
        }
    }

    component Divider: Rectangle {
        width: 270
        height: 1
        color: Theme.c.previewOptionsMenu.border
    }

    Popup {
        id: menu
        x: button.width - width
        y: -height - Theme.spacingMd
        padding: Theme.spacingSm
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

        background: Rectangle {
            color: Theme.c.previewOptionsMenu.background
            border.width: Theme.c.previewOptionsMenu.borderWidth
            border.color: Theme.c.previewOptionsMenu.border
            radius: Theme.c.previewOptionsMenu.radius
        }

        contentItem: Column {
            spacing: 2

            MenuRow {
                text: qsTr("Change Project Aspect Ratio")
                onTriggered: { menu.close(); if (button.panel) button.panel.openVideoSizeDialog() }
            }
            Divider { }
            MenuRow {
                text: qsTr("Use Original Media When Paused")
                showCheck: true
                checked: EditorState.playback.pausedUsesOriginal
                onTriggered: EditorState.playback.pausedUsesOriginal = !EditorState.playback.pausedUsesOriginal
            }
            Divider { }
            MenuRow {
                text: qsTr("Zoom Level")
                hasArrow: true
                onTriggered: zoomList.visible = !zoomList.visible
            }
            Column {
                id: zoomList
                visible: false
                Repeater {
                    model: [{ label: qsTr("Fit"), zoom: 1.0 }, { label: "50%", zoom: 0.5 },
                            { label: "200%", zoom: 2.0 }, { label: "400%", zoom: 4.0 }]
                    delegate: MenuRow {
                        required property var modelData
                        text: "      " + modelData.label
                        showCheck: true
                        checked: button.panel && Math.abs(button.panel.viewportItem.userZoom - modelData.zoom) < 0.01
                        onTriggered: {
                            const v = button.panel.viewportItem
                            if (modelData.zoom === 1.0) v.resetView()
                            else v.zoomAt(v.width / 2, v.height / 2, modelData.zoom / v.userZoom)
                        }
                    }
                }
            }
            MenuRow {
                text: qsTr("Show Safe Zones")
                showCheck: true
                checked: !!button.panel && button.panel.showSafeZones
                onTriggered: button.panel.showSafeZones = !button.panel.showSafeZones
            }
            MenuRow {
                text: qsTr("Ruler")
                shortcut: Theme.platformShortcutText("Ctrl+P")
                showCheck: true
                checked: !!button.panel && button.panel.showRuler
                onTriggered: button.panel.showRuler = !button.panel.showRuler
            }
            MenuRow {
                text: EditorState.preview.guidesEnabled ? qsTr("Hide Guides") : qsTr("Show Guides")
                shortcut: Theme.platformShortcutText("Ctrl+;")
                onTriggered: EditorState.preview.guidesEnabled = !EditorState.preview.guidesEnabled
            }
        }
    }
}
