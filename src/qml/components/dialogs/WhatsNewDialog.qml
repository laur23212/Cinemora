import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin: the first start after an update shows what is new in the version that was installed —
// the notes of its GitHub release (the "what's new" text the Dafin release workflow publishes).
// Settings > Updates > What's new opens it again.
ThemedDialog {
    id: root

    title: qsTr("What's new in Dafin %1").arg(Updates.whatsNewVersion.length > 0 ? Updates.whatsNewVersion
                                                                                : Updates.currentVersion)
    preferredWidth: Theme.dialogWidthMd
    showReject: false
    acceptText: qsTr("Got it")

    onClosed: Updates.dismissWhatsNew()

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : Theme.dialogWidthMd

        ThemedLabel {
            width: parent.width
            size: "base"
            tone: "default"
            wrapMode: Text.WordWrap
            text: qsTr("Dafin was updated to version %1.").arg(Updates.currentVersion)
        }

        Rectangle {
            width: parent.width
            height: Theme.c.updateDialog.borderWidth
            color: Theme.c.updateDialog.divider
        }

        ThemedLabel {
            width: parent.width
            visible: Updates.whatsNewLoading
            size: "sm"
            text: qsTr("Loading the release notes…")
        }

        Flickable {
            id: notesFlick
            width: parent.width
            height: Math.min(notes.implicitHeight, 360)
            contentHeight: notes.implicitHeight
            clip: true
            visible: !Updates.whatsNewLoading
            ScrollBar.vertical: AppScrollBar { }

            ThemedLabel {
                id: notes
                width: notesFlick.width - Theme.spacingLg
                size: "sm"
                tone: "default"
                wrapMode: Text.WordWrap
                textFormat: Text.MarkdownText
                text: Updates.whatsNewNotes.length > 0
                      ? Updates.whatsNewNotes
                      : qsTr("This release has no notes. The full list of changes is on the [releases page](%1).")
                            .arg(Updates.releasesUrl)
                onLinkActivated: (link) => Qt.openUrlExternally(link)
            }
        }
    }
}
