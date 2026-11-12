import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin: shows the app's own log file (the end of it) with a Copy button, and sends diagnostics.
// "report" mode is the fallback when the browser cannot be opened: the full report text, to copy.
ThemedDialog {
    id: root

    // "log" or "report"
    property string mode: "log"

    title: mode === "report" ? qsTr("Diagnostics report") : qsTr("Log viewer")
    preferredWidth: Theme.dialogWidthLg
    acceptText: qsTr("Copy")
    rejectText: qsTr("Close")

    function showLog() {
        mode = "log"
        textArea.text = Diagnostics.logText()
        open()
    }

    function showReport() {
        mode = "report"
        textArea.text = Diagnostics.reportText
        open()
    }

    // Builds the report and opens the browser on a prefilled GitHub issue.
    function send() {
        Diagnostics.sendDiagnostics()
    }

    onAccepted: {
        if (mode === "report")
            Diagnostics.copyReport()
        else
            Diagnostics.copyLog()
        Toasts.success(qsTr("Copied to clipboard"))
    }

    Connections {
        target: Diagnostics
        function onReportReady(browserOpened, truncated, path) {
            if (!browserOpened) {
                // No browser: show the text so it can be copied and sent by hand.
                root.showReport()
                return
            }
            let msg = qsTr("Opened GitHub in your browser. Report saved to %1").arg(path)
            if (truncated)
                msg += qsTr(" The full report is on the clipboard: paste it into the issue.")
            Toasts.success(msg)
        }
    }

    contentItem: Column {
        spacing: Theme.spacingMd
        width: parent ? parent.width : Theme.dialogWidthLg

        ThemedLabel {
            width: parent.width
            size: "sm"
            wrapMode: Text.WordWrap
            text: root.mode === "report"
                  ? qsTr("The browser could not be opened. Copy this report and paste it into a new issue at github.com/%1/issues/new. It is also saved at %2.")
                        .arg(Diagnostics.repo).arg(Diagnostics.reportPath)
                  : qsTr("The end of the log file (%1). It keeps about 8 MB at most. Your user name and personal folders are hidden here.")
                        .arg(Diagnostics.logPath)
        }

        ScrollView {
            id: scroller
            width: parent.width
            height: Math.min(420, root.availableContentHeight - 120)
            clip: true

            ThemedTextArea {
                id: textArea
                readOnly: true
                wrapMode: TextArea.NoWrap
                font.family: "monospace"
                font.pixelSize: Theme.fontSizeXs
            }
        }

        Row {
            spacing: Theme.spacingSm
            visible: root.mode === "log"
            ThemedButton {
                variant: "secondary"
                text: qsTr("Refresh")
                onClicked: textArea.text = Diagnostics.logText()
            }
            ThemedButton {
                variant: "secondary"
                text: qsTr("Open log folder")
                onClicked: Diagnostics.showLogFolder()
            }
            ThemedButton {
                variant: "secondary"
                text: qsTr("Send diagnostics…")
                onClicked: root.send()
            }
        }
    }
}
