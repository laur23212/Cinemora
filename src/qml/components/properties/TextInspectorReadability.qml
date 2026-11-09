import QtQuick
import Drift

// Dafin: when the text becomes readable on this clip, and the two readability rules — fit the
// In / Out to short clips, and an optional cap on how long the entrance may take.
Column {
    id: root
    property var inspector
    width: parent ? parent.width : implicitWidth
    spacing: Theme.spacingSm

    readonly property var info: {
        void root.inspector.animation
        void EditorState.selectedClipData
        if (root.inspector.clipKind === "subtitle")
            void EditorState.inspectorPlayheadSeconds
        return EditorState.textTimingInfo(EditorState.selectedTrack, EditorState.selectedClip) || ({})
    }
    readonly property bool hasReveal: info.hasIn === true || info.hasOut === true
    visible: inspector.hasTextStyle && hasReveal

    function seconds(v) {
        return Number(v || 0).toFixed(2) + " s"
    }

    Text {
        width: parent.width
        wrapMode: Text.WordWrap
        text: root.info.hasIn === true
              ? qsTr("Readable after %1").arg(root.seconds(root.info.readableAt))
              : qsTr("Readable from the first frame")
        color: Theme.panelForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    Text {
        width: parent.width
        visible: root.info.fitted === true
        wrapMode: Text.WordWrap
        text: {
            const parts = []
            if (root.info.hasIn === true && root.info.inDuration < root.info.naturalIn)
                parts.push(qsTr("entrance %1 → %2").arg(root.seconds(root.info.naturalIn)).arg(root.seconds(root.info.inDuration)))
            if (root.info.hasOut === true && root.info.outDuration < root.info.naturalOut)
                parts.push(qsTr("exit %1 → %2").arg(root.seconds(root.info.naturalOut)).arg(root.seconds(root.info.outDuration)))
            return qsTr("Shortened to keep this clip readable: %1").arg(parts.join(", "))
        }
        color: Theme.warning
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    ThemedSwitch {
        text: qsTr("Fit to short clips")
        tooltip: qsTr("Entrance and exit together take at most half of the clip, so the text is never hidden for most of it")
        checked: root.info.fit !== false
        onToggled: EditorState.setTextAnimationTiming(EditorState.selectedTrack, EditorState.selectedClip, { "fit": checked })
    }

    Column {
        width: parent.width
        spacing: 4
        visible: root.info.hasIn === true

        Row {
            spacing: 6
            Text {
                text: qsTr("Readable within")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
            Text {
                text: readSlider.value <= 0 ? qsTr("no limit") : Math.round(readSlider.value) + " ms"
                color: Theme.mutedForeground
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }
        ThemedSlider {
            id: readSlider
            lockWhilePlaying: true
            width: parent.width
            label: qsTr("Readable within")
            from: 0
            to: 1000
            stepSize: 10
            valueFormatter: function (v) { return v <= 0 ? qsTr("no limit") : Math.round(v) + " ms" }
            Binding on value {
                when: !readSlider.pressed
                value: Number(root.info.maxReadMs) || 0
            }
            onMoved: {
                const patch = { "maxReadMs": Math.round(value) }
                if (pressed)
                    EditorState.previewSetTextAnimationTiming(EditorState.selectedTrack, EditorState.selectedClip, patch)
                else
                    EditorState.setTextAnimationTiming(EditorState.selectedTrack, EditorState.selectedClip, patch)
            }
            onPressedChanged: {
                if (pressed)
                    EditorState.beginPreviewDrag(qsTr("Edit text timing"))
                else
                    EditorState.commitPreviewDrag()
            }
        }
        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("Speeds the whole entrance up so the full text shows by then. Captions read best at 300 ms or less.")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }
}
