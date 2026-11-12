import QtQuick
import QtQuick.Controls.Basic
import Drift

// Shows live progress for a background export. Closable while the export keeps
// running; EditorHeader reopens it via the circular-progress badge next to the
// Export button when it's been dismissed mid-export. Cancel stops the encoder.
ThemedDialog {
    id: root

    title: EditorState.exportInProgress ? qsTr("Exporting video") : qsTr("Export")
    preferredWidth: Theme.dialogWidthSm
    // Dafin: set when the export's worker process crashed or hung (exportWorkerFailed); the
    // dialog then says so and its main button retries.
    property string workerProblem: ""
    // Cancel export is the primary destructive action while busy; Close dismisses
    // the dialog without stopping the job (badge in the header reopens it).
    showAccept: EditorState.exportInProgress || workerProblem.length > 0
    acceptText: EditorState.exportInProgress ? qsTr("Cancel export") : qsTr("Retry export")
    acceptVariant: EditorState.exportInProgress ? "destructive" : "primary"
    acceptOnReturn: false
    rejectText: qsTr("Close")
    rejectVariant: "secondary"

    function openDialog() {
        open()
    }

    onAccepted: {
        if (EditorState.exportInProgress)
            EditorState.cancelExport()
        else if (workerProblem.length > 0)
            EditorState.retryExport()
    }

    Connections {
        target: EditorState
        function onExportWorkerFailed(message) { root.workerProblem = message }
        function onExportInProgressChanged() {
            if (EditorState.exportInProgress)
                root.workerProblem = ""
        }
    }

    contentItem: Column {
        spacing: Theme.spacing2xl
        width: parent ? parent.width : 308

        LabelledProgressRing {
            width: parent.width
            value: EditorState.exportProgress
            indeterminate: EditorState.exportInProgress && EditorState.exportProgress <= 0
        }

        ThemedLabel {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            size: "sm"
            wrapMode: Text.WordWrap
            text: EditorState.exportInProgress
                  ? qsTr("Rendering your video. Close to keep editing, or cancel to stop.")
                  : root.workerProblem.length > 0 ? root.workerProblem
                  : qsTr("Export finished.")
        }

        // The finished state used to be that sentence and nothing else — the file was on the
        // device and every way to it was outside the app. Both actions publish to the gallery
        // first, which is a second full copy of the video and the reason it is not done as part
        // of the export.
        //
        // Bound to canShareExport rather than to "the export ended": the publish runs on a
        // worker, so it is false again for as long as either button's copy is in flight, and
        // that is exactly when neither should be pressable.
        Row {
            width: parent.width
            spacing: Theme.androidTouchGap
            visible: !EditorState.exportInProgress && EditorState.canShareExport

            ThemedButton {
                width: (parent.width - parent.spacing) / 2
                height: Theme.androidMinTouchTarget
                variant: "secondary"
                glyph: Theme.icons.play
                text: qsTr("Play")
                onClicked: EditorState.playLastExport()
            }

            ThemedButton {
                width: (parent.width - parent.spacing) / 2
                height: Theme.androidMinTouchTarget
                variant: "primary"
                glyph: Theme.icons.upload
                text: qsTr("Share")
                onClicked: EditorState.shareLastExport()
            }
        }
    }
}
