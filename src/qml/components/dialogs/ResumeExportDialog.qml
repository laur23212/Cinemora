import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin: offered at start-up when a long export did not finish (Dafin crashed or was closed, the
// computer lost power, the export failed). The parts already rendered are kept, so Resume picks up
// from the last finished part; Discard deletes them.
ThemedDialog {
    id: root

    title: qsTr("Unfinished export")
    acceptText: qsTr("Resume export")
    rejectText: qsTr("Discard")
    rejectVariant: "secondary"
    preferredWidth: 440
    closePolicy: Popup.NoAutoClose

    readonly property var info: EditorState.unfinishedExport

    onAccepted: EditorState.resumeUnfinishedExport()
    onRejected: EditorState.discardUnfinishedExport()

    contentItem: Column {
        spacing: Theme.spacingXl
        width: parent ? parent.width : 400

        ThemedLabel {
            width: parent.width
            size: "sm"
            wrapMode: Text.WordWrap
            text: qsTr("An export stopped before it finished. The parts already rendered were kept, so it can carry on from where it stopped.")
        }

        Rectangle {
            width: parent.width
            radius: Theme.c.recoveryDialog.radius
            color: Theme.c.recoveryDialog.background
            border.width: Theme.c.recoveryDialog.borderWidth
            border.color: Theme.c.recoveryDialog.border
            height: infoColumn.implicitHeight + 20

            Column {
                id: infoColumn
                x: 12
                y: 10
                width: parent.width - 24
                spacing: 4

                ThemedLabel {
                    width: parent.width
                    tone: "default"
                    size: "base"
                    elide: Text.ElideMiddle
                    text: (root.info && root.info.name) ? root.info.name : ""
                }

                ThemedLabel {
                    width: parent.width
                    size: "sm"
                    tone: "muted"
                    text: root.info
                          ? qsTr("%1 of %2 parts done (%3%)")
                                .arg(root.info.segmentsDone || 0)
                                .arg(root.info.segmentsTotal || 0)
                                .arg(root.info.percent || 0)
                          : ""
                }

                ThemedLabel {
                    width: parent.width
                    size: "sm"
                    tone: "muted"
                    visible: !!(root.info && root.info.updated && root.info.updated.length > 0)
                    text: qsTr("Last part finished: %1").arg(root.info ? root.info.updated : "")
                }
            }
        }
    }
}
