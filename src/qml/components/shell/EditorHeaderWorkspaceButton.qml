import QtQuick
import Drift

// Dafin: the header's Workspace button. Lists the project's workspaces (separate timelines, each
// with its own undo history), switches between them, opens a new empty one and closes them
// (WorkspacePopup, EditorState.workspaces).
Item {
    id: workspaceButton
    implicitWidth: button.implicitWidth
    implicitHeight: button.implicitHeight
    width: implicitWidth
    height: implicitHeight
    anchors.verticalCenter: parent ? parent.verticalCenter : undefined

    IconButton {
        id: button
        anchors.fill: parent
        glyph: Theme.icons.layers
        variant: "ghost"
        text: EditorState.workspaces.count > 1
              ? qsTr("Workspace (%1)").arg(EditorState.workspaces.count)
              : qsTr("Workspace")
        active: popup.opened
        tooltip: qsTr("Workspaces — now in “%1”").arg(EditorState.workspaces.activeName)
        onClicked: popup.opened ? popup.close() : popup.open()
    }

    WorkspacePopup {
        id: popup
        x: 0
        y: workspaceButton.height + Theme.spacingMd
        onCloseRequested: (workspaceId, name, unsaved) => {
            if (!unsaved) {
                EditorState.workspaces.close(workspaceId)
                return
            }
            confirmClose.workspaceId = workspaceId
            confirmClose.workspaceName = name
            confirmClose.open()
        }
    }

    ThemedDialog {
        id: confirmClose
        property string workspaceId: ""
        property string workspaceName: ""
        title: qsTr("Close “%1”?").arg(confirmClose.workspaceName)
        acceptText: qsTr("Close workspace")
        acceptVariant: "destructive"
        preferredWidth: Theme.dialogWidthSm
        // Enter must not throw work away.
        acceptOnReturn: false

        contentItem: ThemedLabel {
            width: parent ? parent.width : Theme.dialogWidthSm
            wrapMode: Text.WordWrap
            size: "sm"
            text: qsTr("This workspace has changes that are not saved. Closing it throws its timeline and undo history away. A render queued from it keeps going.")
        }

        onAccepted: {
            EditorState.workspaces.close(confirmClose.workspaceId)
            confirmClose.workspaceId = ""
        }
        onRejected: confirmClose.workspaceId = ""
    }
}
