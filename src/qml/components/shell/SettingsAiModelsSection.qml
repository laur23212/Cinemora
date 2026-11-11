import QtQuick
import Drift

// Dafin: the AI models and engines downloaded so far, each with its size and a Delete button
// (pressed twice, so one slip does not throw away a large download). They are add-ons in the app
// data folder; one that is deleted downloads again the next time a tool needs it.
Column {
    id: root

    spacing: Theme.spacingMd

    // What counts as AI here: the engine (ONNX Runtime and its GPU parts) and every model kind
    // the AI tools load.
    readonly property var aiKinds: ["onnxruntime", "onnxruntime-ep", "whisper-model", "align-model",
                                    "vad-model", "diarize-model", "denoise-model", "sam2-model",
                                    "rvm-model", "restore-model", "tts-model", "face-model",
                                    "depth-model", "audio-tag-model", "audio-class-model", "object-model"]

    readonly property var rows: {
        const out = []
        const catalog = Addons.catalog
        for (let i = 0; i < catalog.length; ++i) {
            const row = catalog[i]
            if (row.state !== "installed" && row.state !== "update-available")
                continue
            const kinds = row.kinds && row.kinds.length > 0 ? row.kinds : [row.kind]
            if (kinds.some(k => root.aiKinds.indexOf(k) >= 0))
                out.push(row)
        }
        out.sort((a, b) => Number(b.installedSize || 0) - Number(a.installedSize || 0))
        return out
    }
    readonly property real totalBytes: rows.reduce((sum, row) => sum + Number(row.installedSize || 0), 0)

    // The row whose Delete was pressed once and now asks to be pressed again.
    property string confirmId: ""
    property bool engineRemoved: false
    property bool stillListening: false

    function kindsOf(row) {
        return row.kinds && row.kinds.length > 0 ? row.kinds : [row.kind]
    }
    function isEngine(row) {
        const kinds = root.kindsOf(row)
        return kinds.indexOf("onnxruntime") >= 0 || kinds.indexOf("onnxruntime-ep") >= 0
    }
    function isSpeech(row) {
        const kinds = root.kindsOf(row)
        return kinds.indexOf("whisper-model") >= 0 || kinds.indexOf("align-model") >= 0
               || kinds.indexOf("diarize-model") >= 0
    }

    function remove(row) {
        // A loaded speech model holds its files open; let go of it first, or wait for the run.
        if (root.isSpeech(row) && !EditorState.ai.releaseSpeechModels()) {
            root.stillListening = true
            return
        }
        root.stillListening = false
        if (root.isEngine(row))
            root.engineRemoved = true
        Addons.uninstall(row.id)
    }

    Timer {
        id: confirmTimer
        interval: 4000
        onTriggered: root.confirmId = ""
    }

    Text {
        width: root.width
        wrapMode: Text.WordWrap
        color: Theme.c.subtitlesTab.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        text: root.rows.length === 0
              ? qsTr("Nothing downloaded yet. Each AI tool downloads its model the first time you use it.")
              : qsTr("%1 in use on this computer.").arg(DownloadFormat.formatBytes(root.totalBytes))
    }

    Repeater {
        model: root.rows

        delegate: Item {
            id: entry

            required property var modelData
            readonly property bool confirming: root.confirmId === entry.modelData.id

            width: root.width
            height: Math.max(names.implicitHeight, deleteButton.implicitHeight)

            Column {
                id: names
                anchors.left: parent.left
                anchors.right: deleteButton.left
                anchors.rightMargin: Theme.spacingMd
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2

                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    text: entry.modelData.name || entry.modelData.id
                    color: Theme.c.subtitlesTab.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                }

                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    color: Theme.c.subtitlesTab.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    text: {
                        const bits = []
                        const size = DownloadFormat.formatBytes(Number(entry.modelData.installedSize || 0))
                        if (size.length > 0)
                            bits.push(size)
                        if (root.isEngine(entry.modelData))
                            bits.push(qsTr("AI engine"))
                        if (entry.modelData.state === "update-available")
                            bits.push(qsTr("update available in Extras"))
                        return bits.join(" · ")
                    }
                }
            }

            ThemedButton {
                id: deleteButton
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                enabled: !EditorState.ai.captioning
                variant: entry.confirming ? "destructive" : "secondary"
                glyph: Theme.icons.trash
                text: entry.confirming ? qsTr("Delete now") : qsTr("Delete")
                tooltip: entry.confirming
                         ? qsTr("Press again to delete it from this computer")
                         : qsTr("Delete it from this computer. It downloads again when a tool needs it.")
                onClicked: {
                    if (!entry.confirming) {
                        root.confirmId = entry.modelData.id
                        confirmTimer.restart()
                        return
                    }
                    root.confirmId = ""
                    root.remove(entry.modelData)
                }
            }
        }
    }

    Text {
        width: root.width
        visible: root.stillListening
        wrapMode: Text.WordWrap
        color: Theme.c.scenesTab.warning
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        text: qsTr("Speech recognition is still working. Delete it once it has finished.")
    }

    // The engine stays loaded until Dafin closes, so its files are only gone for good then.
    Text {
        width: root.width
        visible: root.engineRemoved
        wrapMode: Text.WordWrap
        color: Theme.c.scenesTab.warning
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        text: qsTr("Restart Dafin to finish removing the AI engine.")
    }
}
