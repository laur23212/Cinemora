import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin: asked after importing heavy clips (4K, HEVC, 10-bit, very high bitrate). Two answers:
// make low-res proxies for smooth editing, or preview the original at full quality. Escape or a
// click outside means full quality (nothing is made). "Remember my choice" saves the answer in
// Settings › Preview › After importing heavy clips.
ThemedDialog {
    id: root

    property var assetIds: []
    property var rows: []
    property bool _answered: false

    title: qsTr("%n clip(s) may play back slowly", "", rows.length)
    preferredWidth: 480
    showFooter: false
    showAccept: false
    showReject: false
    acceptOnReturn: false

    function ask(ids) {
        root.assetIds = ids
        root.rows = DafinProxies.describe(ids)
        root._answered = false
        rememberBox.checked = false
        if (root.rows.length === 0)
            return
        root.open()
    }

    function answer(useProxies) {
        if (root._answered)
            return
        root._answered = true
        if (rememberBox.checked)
            DafinProxies.importChoice = useProxies ? "proxy" : "full"
        if (useProxies)
            AssetLibrary.createProxies(root.assetIds)
        root.close()
    }

    onClosed: root.answer(false)

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : 448

        Repeater {
            model: root.rows

            ThemedLabel {
                required property var modelData
                width: parent.width
                size: "sm"
                elide: Text.ElideMiddle
                text: modelData.reasons.length > 0
                      ? qsTr("%1  ·  %2").arg(modelData.name).arg(modelData.reasons)
                      : modelData.name
            }
        }

        ThemedLabel {
            width: parent.width
            size: "sm"
            wrapMode: Text.WordWrap
            text: qsTr("A proxy is a small %1p copy made in the background and used only while you edit. Export always uses the original files.")
                  .arg(EditorState.playback.proxySize)
        }

        ThemedLabel {
            width: parent.width
            size: "xs"
            wrapMode: Text.WordWrap
            text: qsTr("<b>Proxy:</b> smooth playback and scrubbing, less load on your computer. "
                       + "It takes a few minutes to make, uses disk space in the proxy cache and the preview looks a little softer.")
        }

        ThemedLabel {
            width: parent.width
            size: "xs"
            wrapMode: Text.WordWrap
            text: qsTr("<b>Full quality:</b> the sharpest preview and nothing to wait for, but these clips may stutter while playing or scrubbing.")
        }

        ThemedCheckBox {
            id: rememberBox
            text: qsTr("Remember my choice (change it in Settings › Preview)")
        }

        Row {
            anchors.right: parent.right
            spacing: Theme.spacingLg

            ThemedButton {
                text: qsTr("Use full quality")
                variant: "secondary"
                onClicked: root.answer(false)
            }

            ThemedButton {
                text: qsTr("Create proxies")
                variant: "primary"
                onClicked: root.answer(true)
            }
        }
    }
}
