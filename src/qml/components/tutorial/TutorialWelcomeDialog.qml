import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin: shown once, the first time the editor opens. Take a look opens the Tutorial's feature
// list; Skip (or Escape) closes it. Either way it does not come back.
ThemedDialog {
    id: root

    title: qsTr("Welcome to Dafin")
    preferredWidth: Theme.dialogWidthMd
    acceptText: qsTr("Take a look")
    rejectText: qsTr("Skip")
    closePolicy: Popup.CloseOnEscape

    onAccepted: {
        if (ShellBridge.main && ShellBridge.main.openTutorial)
            Qt.callLater(ShellBridge.main.openTutorial, "")
    }

    contentItem: ThemedLabel {
        width: parent ? parent.width : Theme.dialogWidthMd
        size: "base"
        tone: "default"
        text: qsTr("A short tutorial shows where things are and what they do, with guided tours that point at the real buttons. You can open it any time from the Tutorial button in the top bar.")
    }
}
