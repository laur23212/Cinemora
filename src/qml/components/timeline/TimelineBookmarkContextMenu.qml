import QtQuick
import Drift

// Context menu for one bookmark flag (a "marker" in Filmora's words).
ThemedContextMenu {
    id: menu

    property int bookmarkIndex: -1
    property string bookmarkLabel: ""
    property real bookmarkSeconds: 0
    property string bookmarkColor: ""

    signal renameRequested(int index, string label)

    // Dafin: marker colours, as in Filmora's marker colour picker. "" = the theme's own.
    readonly property var markerColors: ["", "#e5484d", "#f5a524", "#f5d90a", "#46a758",
                                         "#00b8a9", "#3e63dd", "#8e4ec6", "#d6409f"]

    ThemedMenuItem {
        text: qsTr("Go to bookmark")
        icon.name: Theme.icons.bookmark
        onTriggered: EditorState.goToBookmark(menu.bookmarkIndex)
    }
    ThemedMenuItem {
        text: qsTr("Previous marker")
        icon.name: Theme.icons.chevronLeft
        onTriggered: EditorState.goToPreviousBookmark()
    }
    ThemedMenuItem {
        text: qsTr("Next marker")
        icon.name: Theme.icons.chevronRight
        onTriggered: EditorState.goToNextBookmark()
    }
    ThemedMenuItem {
        text: qsTr("Rename…")
        onTriggered: menu.renameRequested(menu.bookmarkIndex, menu.bookmarkLabel)
    }
    ThemedMenuSeparator { }

    // Colour swatches: one click recolours the marker (one undo step) and closes the menu.
    Item {
        width: swatchRow.implicitWidth + 2 * Theme.spacingSm
        height: swatchRow.implicitHeight + 2 * Theme.spacingXs

        Row {
            id: swatchRow
            anchors.centerIn: parent
            spacing: 4

            Repeater {
                model: menu.markerColors
                delegate: Rectangle {
                    id: swatch
                    required property string modelData
                    readonly property bool current: menu.bookmarkColor.toLowerCase() === modelData
                    width: 16
                    height: 16
                    radius: 8
                    color: modelData === "" ? Theme.c.timelineBookmarkMarker.foreground : modelData
                    border.width: current || swatchMouse.containsMouse ? 2 : 0
                    border.color: Theme.c.timelineBookmarkMarker.border

                    ThemedToolTip {
                        visible: swatchMouse.containsMouse
                        text: swatch.modelData === "" ? qsTr("Default colour") : qsTr("Marker colour")
                    }
                    MouseArea {
                        id: swatchMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            EditorState.setBookmarkColor(menu.bookmarkIndex, swatch.modelData)
                            menu.close()
                        }
                    }
                }
            }
        }
    }

    ThemedMenuSeparator { }
    ThemedMenuItem {
        text: qsTr("Delete")
        icon.name: Theme.icons.trash
        onTriggered: EditorState.removeBookmark(menu.bookmarkIndex)
    }
}
