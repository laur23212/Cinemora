import QtQuick
import Drift

// Dafin: "Open its timeline" for a row of the export queue. Switches to the Render timeline
// workspace the job was queued with (EditorState.workspaces.addRenderTimeline returns its id).
// Once that workspace is closed the link says so; the render itself is not affected.
//
//     OpenRenderTimelineLink { workspaceId: job.workspaceId }
Text {
    id: root

    property string workspaceId: ""

    readonly property bool available: EditorState.workspaces.revision >= 0
                                      && EditorState.workspaces.exists(root.workspaceId)
    readonly property bool isOpen: root.available && EditorState.workspaces.activeId === root.workspaceId

    visible: root.workspaceId !== ""
    text: !root.available ? qsTr("Timeline closed")
                          : root.isOpen ? qsTr("Its timeline is open") : qsTr("Open its timeline")
    color: root.available && !root.isOpen ? Theme.accent : Theme.mutedForeground
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSizeXs
    font.underline: linkArea.containsMouse && root.available && !root.isOpen

    MouseArea {
        id: linkArea
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.available && !root.isOpen
        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: EditorState.workspaces.switchTo(root.workspaceId)
    }
}
