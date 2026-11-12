import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin: shown when a project file will not open but one of its rotating backups (the backups
// folder beside it) still does. Opening the backup keeps the project's name and place: Save then
// replaces the damaged file.
ThemedDialog {
    id: root

    property string projectPath: ""
    property string backupPath: ""
    property string backupTime: ""

    title: qsTr("This project couldn’t be opened")
    acceptText: qsTr("Open backup")
    rejectText: qsTr("Cancel")
    rejectVariant: "secondary"
    preferredWidth: 440

    function openFor(project, backup, time) {
        projectPath = project
        backupPath = backup
        backupTime = time
        open()
    }

    onAccepted: EditorState.projectFile.openBackup(projectPath, backupPath)

    contentItem: Column {
        spacing: Theme.spacingXl
        width: parent ? parent.width : 400

        ThemedLabel {
            width: parent.width
            size: "sm"
            wrapMode: Text.WordWrap
            text: qsTr("The file is damaged or unreadable. The newest good backup of it, saved %1, can be opened instead. Saving afterwards replaces the damaged file.").arg(root.backupTime)
        }

        ThemedLabel {
            width: parent.width
            size: "sm"
            tone: "muted"
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideMiddle
            text: root.projectPath
        }
    }
}
