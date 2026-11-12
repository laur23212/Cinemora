import QtQuick
import Drift

// Dafin: the export queue's header button — shown once something is queued, with how many are
// waiting or rendering — and its toasts: added (Show queue), finished or failed (Open folder).
Item {
    id: queueButton
    readonly property int pending: ExportQueue.pendingCount
    // Read by the header's BarSlot so the slot hides with the button (see BarSlot).
    readonly property bool barVisible: ExportQueue.count > 0
    visible: barVisible
    implicitWidth: queueBtn.implicitWidth
    implicitHeight: queueBtn.implicitHeight
    width: barVisible ? implicitWidth : 0
    height: implicitHeight
    anchors.verticalCenter: parent ? parent.verticalCenter : undefined

    function showPanel() {
        panelLoader.ensure().openDialog()
    }

    IconButton {
        id: queueBtn
        anchors.fill: parent
        glyph: Theme.icons.listVideo
        variant: "ghost"
        active: ExportQueue.busy
        tooltip: ExportQueue.busy
                 ? qsTr("Export queue — rendering %1%, %n waiting", "", Math.max(0, queueButton.pending - 1))
                   .arg(Math.round(ExportQueue.currentProgress * 100))
                 : qsTr("Export queue")
        onClicked: queueButton.showPanel()
    }

    Rectangle {
        visible: queueButton.pending > 0
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spacingXs
        width: Math.max(Theme.spacingXl, pendingCount.implicitWidth + Theme.spacingSm)
        height: Theme.spacingXl
        radius: Theme.c.editorHeaderDownloadsButton.badgeRadius
        color: Theme.c.editorHeaderDownloadsButton.badge

        Text {
            id: pendingCount
            anchors.centerIn: parent
            text: String(queueButton.pending)
            color: Theme.c.editorHeaderDownloadsButton.badgeForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }

    LazyLoader {
        id: panelLoader
        sourceComponent: Component { ExportQueuePanel { } }
    }

    Connections {
        target: ExportQueue
        function onShowPanelRequested() { queueButton.showPanel() }
        function onMessage(text, severity) {
            switch (severity) {
            case "error":   Toasts.error(text); break
            case "warning": Toasts.warning(text); break
            case "success": Toasts.success(text); break
            default:        Toasts.info(text); break
            }
        }
        function onJobAdded(jobId, name) {
            Toasts.post("info", qsTr("Added “%1” to the export queue").arg(name), Toasts.defaultTimeout,
                        qsTr("Show queue"), function () { queueButton.showPanel() })
        }
        function onJobFinished(jobId, ok, cancelled, usedCpuFallback, name, message, outputPath) {
            if (cancelled)
                return
            const severity = ok ? "success" : "error"
            const fileName = outputPath.split(/[\\/]/).pop()
            const text = ok ? (usedCpuFallback
                               ? qsTr("Exported %1 — the hardware encoder failed, so the CPU encoder was used").arg(fileName)
                               : qsTr("Exported %1").arg(fileName))
                            : qsTr("%1: %2").arg(fileName).arg(message)
            Toasts.post(severity, text, ok ? Toasts.defaultTimeout : 0,
                        qsTr("Open folder"), function () { ExportQueue.openOutputFolder(jobId) })
        }
    }
}
