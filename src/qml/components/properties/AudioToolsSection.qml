import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin's local audio tools in the Audio tab: Loudness (loudnorm), Noise reduction (afftdn) and
// Silence remover (silencedetect + cut). All FFmpeg, no model to download. EditorState.audioTools.
Column {
    id: root

    property int trackIndex: -1
    property int clipIndex: -1
    // Bumped by the Audio tab whenever the selection or the timeline changes.
    property int revision: 0

    readonly property var tools: EditorState.audioTools
    readonly property bool usable: { void revision; return tools.canProcess(trackIndex, clipIndex) }
    readonly property string audibleId: { void revision; return tools.clipIdAt(trackIndex, clipIndex) }
    // The silence preview belongs to this clip only while it is the one selected.
    readonly property bool previewHere: audibleId !== "" && tools.silencePreviewClipId === audibleId
    readonly property var presets: [
        { label: qsTr("Online video (YouTube, TikTok) · −14 LUFS"), lufs: -14 },
        { label: qsTr("Podcast and voice · −16 LUFS"), lufs: -16 },
        { label: qsTr("Mobile, louder · −11 LUFS"), lufs: -11 },
        { label: qsTr("TV broadcast (EBU R128) · −23 LUFS"), lufs: -23 },
        { label: qsTr("Cinema and quiet · −27 LUFS"), lufs: -27 }
    ]

    spacing: Theme.spacingSm
    visible: usable

    component Heading: Text {
        width: root.width
        color: Theme.c.audioInspector.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }
    component Note: Text {
        width: root.width
        wrapMode: Text.WordWrap
        color: Theme.c.audioInspector.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        opacity: 0.8
    }

    // ----- Loudness ----------------------------------------------------
    SearchAnchor {
        searchId: "audio-loudness-normalize"
        title: qsTr("Normalize loudness")
        keywords: "loudnorm lufs volume level youtube podcast ebu r128"
        target: loudnessPreset
    }
    Heading { text: qsTr("Loudness") }
    ThemedComboBox {
        id: loudnessPreset
        width: root.width
        textRole: "label"
        model: root.presets
        currentIndex: 0
    }
    ThemedButton {
        width: root.width
        text: qsTr("Normalize loudness")
        enabled: !root.tools.running
        onClicked: root.tools.normalizeLoudness(root.trackIndex, root.clipIndex,
                                                root.presets[loudnessPreset.currentIndex].lufs)
    }

    // ----- Noise reduction ---------------------------------------------
    SearchAnchor {
        searchId: "audio-noise-reduction"
        title: qsTr("Reduce noise")
        keywords: "afftdn hiss hum fan background noise denoise"
        target: noiseStrength
    }
    Heading { text: qsTr("Noise reduction") }
    ThemedSlider {
        id: noiseStrength
        width: root.width
        label: qsTr("Strength")
        from: 0
        to: 1
        value: 0.5
        valueFormatter: function (v) { return Math.round(v * 100) + "%" }
    }
    ThemedButton {
        width: root.width
        text: qsTr("Reduce noise")
        enabled: !root.tools.running
        onClicked: root.tools.reduceNoise(root.trackIndex, root.clipIndex, noiseStrength.value)
    }

    // ----- Silence remover ---------------------------------------------
    SearchAnchor {
        searchId: "audio-silence-remover"
        title: qsTr("Silence remover")
        keywords: "silencedetect remove silence gaps pauses jump cut"
        target: silenceThreshold
    }
    Heading { text: qsTr("Silence remover") }
    ThemedSlider {
        id: silenceThreshold
        width: root.width
        label: qsTr("Quieter than")
        from: -70
        to: -20
        stepSize: 1
        value: -40
        valueFormatter: function (v) { return Math.round(v) + " dB" }
        onMoved: root.tools.clearSilencePreview()
    }
    ThemedSlider {
        id: silenceMinLength
        width: root.width
        label: qsTr("For at least")
        from: 0.2
        to: 3
        stepSize: 0.1
        value: 0.6
        valueFormatter: function (v) { return Number(v).toFixed(1) + " s" }
        onMoved: root.tools.clearSilencePreview()
    }
    ThemedSlider {
        id: silencePadding
        width: root.width
        label: qsTr("Keep around speech")
        from: 0
        to: 0.5
        stepSize: 0.01
        value: 0.1
        valueFormatter: function (v) { return Number(v).toFixed(2) + " s" }
        onMoved: root.tools.clearSilencePreview()
    }
    ThemedButton {
        width: root.width
        text: root.previewHere ? qsTr("Find again") : qsTr("Find silence")
        enabled: !root.tools.running
        onClicked: root.tools.findSilence(root.trackIndex, root.clipIndex, silenceThreshold.value,
                                          silenceMinLength.value, silencePadding.value)
    }

    // What will be cut: the clip as a bar, the silent parts marked on it. Click a mark to hear it.
    Item {
        width: root.width
        height: Theme.spacingXl * 1.5
        visible: root.previewHere && root.tools.silencePreview.length > 0

        Rectangle {
            anchors.fill: parent
            radius: 3
            color: Theme.c.audioInspector.foregroundMuted
            opacity: 0.25
        }
        Repeater {
            model: root.previewHere ? root.tools.silencePreview : []
            delegate: Rectangle {
                required property var modelData
                required property int index
                readonly property real total: Math.max(0.001, root.tools.silencePreviewClipSeconds)
                x: parent.width * modelData.start / total
                width: Math.max(2, parent.width * (modelData.end - modelData.start) / total)
                height: parent.height
                radius: 2
                color: Theme.warning
                opacity: cutMouse.containsMouse ? 1.0 : 0.75
                MouseArea {
                    id: cutMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.tools.seekToSilence(index)
                }
                ThemedToolTip {
                    visible: cutMouse.containsMouse
                    text: qsTr("%1 s – %2 s (click to listen)")
                          .arg(Number(modelData.start).toFixed(2)).arg(Number(modelData.end).toFixed(2))
                }
            }
        }
    }
    Note {
        visible: root.previewHere
        text: root.tools.silencePreview.length > 0
              ? qsTr("%1 silent parts, %2 s of %3 s will be cut. The clip and its linked clips close up after each cut.")
                    .arg(root.tools.silencePreview.length)
                    .arg(Number(root.tools.silencePreviewSeconds).toFixed(1))
                    .arg(Number(root.tools.silencePreviewClipSeconds).toFixed(1))
              : qsTr("No silence found with these settings.")
    }
    Row {
        width: root.width
        spacing: Theme.spacingSm
        visible: root.previewHere && root.tools.silencePreview.length > 0
        ThemedButton {
            width: (root.width - parent.spacing) / 2
            text: qsTr("Remove silence")
            variant: "primary"
            enabled: !root.tools.running
            onClicked: root.tools.applySilenceRemoval()
        }
        ThemedButton {
            width: (root.width - parent.spacing) / 2
            text: qsTr("Clear")
            enabled: !root.tools.running
            onClicked: root.tools.clearSilencePreview()
        }
    }

    // ----- Shared progress ---------------------------------------------
    Row {
        width: root.width
        spacing: Theme.spacingSm
        visible: root.tools.running
        ThemedProgressBar {
            width: root.width - cancelButton.width - parent.spacing
            anchors.verticalCenter: parent.verticalCenter
            value: root.tools.progress
        }
        ThemedButton {
            id: cancelButton
            text: qsTr("Cancel")
            onClicked: root.tools.cancel()
        }
    }
    Note {
        visible: root.tools.running
        text: root.tools.status
    }
}
