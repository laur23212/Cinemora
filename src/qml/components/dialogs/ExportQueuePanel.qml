import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin: the export queue. Each row is one queued export, rendered in the background from a copy
// of the project taken when it was added: progress, Cancel, Remove, move up/down (the next job is
// the first waiting one from the top) and Open folder. Closing it doesn't stop anything.
ThemedDialog {
    id: root

    title: qsTr("Export queue")
    preferredWidth: Theme.dialogWidthLg
    showReject: false
    acceptText: qsTr("Close")
    acceptVariant: "secondary"

    function openDialog() {
        open()
    }

    footerExtra: [
        ThemedButton {
            visible: ExportQueue.count > ExportQueue.pendingCount
            text: qsTr("Clear finished")
            variant: "ghost"
            onClicked: ExportQueue.clearFinished()
        }
    ]

    contentItem: Item {
        width: parent ? parent.width : Theme.dialogWidthLg
        implicitHeight: Math.min(Math.max(emptyLabel.implicitHeight, jobList.contentHeight),
                                 Math.max(160, root.availableContentHeight))

        ThemedLabel {
            id: emptyLabel
            anchors.centerIn: parent
            visible: ExportQueue.count === 0
            size: "sm"
            text: qsTr("Nothing queued. In the Export dialog, choose Add to queue to render in the background while you keep editing.")
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
        }

        ListView {
            id: jobList
            anchors.fill: parent
            clip: true
            spacing: Theme.spacingLg
            model: ExportQueue
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar { }

            delegate: Rectangle {
                id: row
                required property int index
                required property string jobId
                required property string name
                required property string outputName
                required property string folder
                required property string status
                required property real progress
                required property string statusText
                required property bool usedCpuFallback
                required property string summary

                readonly property bool waiting: status === "queued"
                readonly property bool running: status === "running"
                readonly property bool finished: !waiting && !running

                width: ListView.view.width - Theme.spacingLg
                height: rowColumn.implicitHeight + 2 * Theme.spacingLg
                radius: Theme.radiusMd
                color: Theme.panelSecondaryBg
                border.color: Theme.panelSecondaryBorder
                border.width: 1

                Column {
                    id: rowColumn
                    anchors.left: parent.left
                    anchors.right: actions.left
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: Theme.spacingLg
                    anchors.rightMargin: Theme.spacingLg
                    spacing: Theme.spacingSm

                    ThemedLabel {
                        width: parent.width
                        tone: "default"
                        size: "sm"
                        elide: Text.ElideMiddle
                        wrapMode: Text.NoWrap
                        text: (row.index + 1) + ". " + row.outputName
                    }

                    ThemedLabel {
                        width: parent.width
                        elide: Text.ElideRight
                        wrapMode: Text.NoWrap
                        text: row.name + " · " + row.summary
                    }

                    ThemedProgressBar {
                        width: parent.width
                        visible: row.running || row.status === "done"
                        value: row.progress
                    }

                    ThemedLabel {
                        width: parent.width
                        color: row.status === "failed" ? Theme.c.toast.error
                               : row.usedCpuFallback ? Theme.c.toast.warning
                               : Theme.c.themedLabel.foregroundMuted
                        text: row.statusText
                    }
                }

                Row {
                    id: actions
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingSm
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacingXs

                    IconButton {
                        glyph: Theme.icons.chevronUp
                        tooltip: qsTr("Move up")
                        enabled: row.index > 0
                        onClicked: ExportQueue.move(row.index, row.index - 1)
                    }
                    IconButton {
                        glyph: Theme.icons.chevronDown
                        tooltip: qsTr("Move down")
                        enabled: row.index < ExportQueue.count - 1
                        onClicked: ExportQueue.move(row.index, row.index + 1)
                    }
                    IconButton {
                        glyph: Theme.icons.folder
                        tooltip: qsTr("Open output folder")
                        onClicked: ExportQueue.openOutputFolder(row.jobId)
                    }
                    IconButton {
                        visible: !row.finished
                        glyph: Theme.icons.x
                        tooltip: row.running ? qsTr("Cancel this export") : qsTr("Cancel (keeps it in the list)")
                        onClicked: ExportQueue.cancel(row.jobId)
                    }
                    IconButton {
                        visible: !row.running
                        glyph: Theme.icons.trash
                        tooltip: qsTr("Remove from the queue")
                        onClicked: ExportQueue.remove(row.jobId)
                    }
                }
            }
        }
    }
}
