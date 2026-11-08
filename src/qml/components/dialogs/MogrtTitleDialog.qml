import QtQuick
import QtQuick.Controls.Basic
import QtMultimedia
import Drift

// Dafin: opened from the Titles grid for a .mogrt that can't be added as an animated template
// title here: made in Premiere Pro, MOGRT Converter not installed, or the converter failed on it
// (EditorState.mogrt.templateStatus says which). It says why and offers what is possible: an
// ordinary editable Dafin title with the template's text, getting the converter, or trying again.
// Templates that do render never come here; they go straight to the timeline, editable.
ThemedDialog {
    id: root

    property var item: ({})
    readonly property var extra: item && item.extra ? item.extra : ({})
    readonly property string mogrtPath: item && item.downloadUrl ? MogrtRenderer.pathFor(item.downloadUrl) : ""
    readonly property var status: mogrtPath.length > 0
                                  ? (EditorState.mogrt.revision, EditorState.mogrt.templateStatus(item.id || "", mogrtPath))
                                  : ({})
    readonly property string mogrtState: status.state || ""
    readonly property bool animates: ["checking", "ok", "warnings"].indexOf(mogrtState) >= 0
    property var fields: []

    title: item && item.title ? item.title : qsTr("Title template")
    preferredWidth: Theme.dialogWidthMd
    showFooter: false
    acceptOnReturn: false

    function openFor(templateItem) {
        root.item = templateItem
        const texts = (templateItem.extra && templateItem.extra.texts) || []
        const list = []
        for (let i = 0; i < texts.length; ++i)
            list.push({ id: texts[i].id || "", label: texts[i].label || "", text: texts[i].text || "" })
        if (list.length === 0)
            list.push({ id: "", label: qsTr("Text"), text: (templateItem.extra && templateItem.extra.text) || templateItem.title || "" })
        root.fields = list
        lengthField.value = Math.max(0.5, Number((templateItem.extra && templateItem.extra.duration) || 5))
        MogrtRenderer.refresh()
        root.open()
    }

    function editedText() {
        const lines = []
        for (let i = 0; i < root.fields.length; ++i) {
            const t = (root.fields[i].text || "").trim()
            if (t.length > 0)
                lines.push(t)
        }
        return lines.join("\n")
    }

    function addEditable() {
        EditorState.addTemplateTitle(root.editedText(), root.extra.preset || "title", lengthField.value, root.item.title || "")
        root.close()
    }

    function addTemplate() {
        const result = EditorState.mogrt.addTemplate(root.mogrtPath, -1, -1, root.item.title || "")
        if (result && result.ok)
            root.close()
    }

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : Theme.dialogWidthMd

        Rectangle {
            id: previewBox
            width: parent.width
            height: Math.round(width * 9 / 16)
            radius: Theme.radiusSm
            clip: true
            color: Theme.c.assetsPanel.pickerBackground
            border.width: 1
            border.color: Theme.panelBorder

            Image {
                anchors.fill: parent
                anchors.margins: 1
                source: root.item && root.item.thumbnailUrl ? root.item.thumbnailUrl : ""
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                visible: status === Image.Ready && !videoOut.visible
            }
            MediaPlayer {
                id: player
                source: root.visible && root.item && root.item.previewUrl ? root.item.previewUrl : ""
                videoOutput: videoOut
                loops: MediaPlayer.Infinite
                audioOutput: AudioOutput { muted: true }
                onSourceChanged: if (source.toString().length > 0) play()
            }
            VideoOutput {
                id: videoOut
                anchors.fill: parent
                anchors.margins: 1
                fillMode: VideoOutput.PreserveAspectFit
                visible: player.playbackState === MediaPlayer.PlayingState && player.hasVideo
            }
        }

        Repeater {
            model: root.fields.length
            delegate: Column {
                required property int index
                width: parent ? parent.width : 0
                spacing: Theme.spacingXs
                ThemedLabel {
                    size: "sm"
                    text: root.fields[index].label.length > 0 ? root.fields[index].label : qsTr("Text %1").arg(index + 1)
                }
                ThemedTextArea {
                    width: parent.width
                    wrapMode: TextEdit.Wrap
                    text: root.fields[index].text
                    onTextChanged: root.fields[index].text = text
                }
            }
        }

        Row {
            spacing: Theme.spacingSm
            ThemedLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Length")
            }
            ThemedNumberField {
                id: lengthField
                width: 110
                from: 0.5
                to: 300
                step: 0.5
                decimals: 1
                unit: qsTr("s")
            }
        }

        ThemedLabel {
            width: parent.width
            size: "sm"
            wrapMode: Text.WordWrap
            text: root.animates
                  ? qsTr("MOGRT Converter can animate this template now. Add it as a template title to keep its animation, with the text and other controls editable in Properties › Template.")
                  : root.mogrtState === "unsupported"
                    ? qsTr("This template was made in Premiere Pro. MOGRT Converter only animates templates made in After Effects, so Dafin can add it as an editable title with your text, without the animation.")
                    : root.mogrtState === "needsConverter"
                      ? qsTr("Dafin animates templates with MOGRT Converter, a free program installed separately. Without it, the template can be added as an editable title with your text, without the animation.")
                      : (root.status.message ? qsTr("MOGRT Converter couldn't render this template: %1").arg(root.status.message)
                                             : qsTr("This template can't be rendered."))
        }

        Row {
            visible: root.mogrtState === "needsConverter" || root.mogrtState === "failed"
            spacing: Theme.spacingSm
            ThemedButton {
                visible: root.mogrtState === "needsConverter"
                variant: "secondary"
                glyph: Theme.icons.externalLink
                text: qsTr("Get MOGRT Converter")
                onClicked: Qt.openUrlExternally(MogrtRenderer.downloadUrl)
            }
            ThemedButton {
                variant: "ghost"
                glyph: Theme.icons.refresh
                text: qsTr("Check again")
                onClicked: {
                    MogrtRenderer.refresh()
                    if (!MogrtRenderer.available)
                        Toasts.info(qsTr("MOGRT Converter wasn't found. If it is installed somewhere else, choose it in Settings > Asset Providers."))
                }
            }
        }

        Item {
            width: parent.width
            height: closeButton.height

            ThemedButton {
                id: closeButton
                anchors.left: parent.left
                variant: "ghost"
                text: qsTr("Close")
                onClicked: root.close()
            }

            Row {
                anchors.right: parent.right
                spacing: Theme.spacingLg

                ThemedButton {
                    variant: root.animates ? "secondary" : "primary"
                    text: qsTr("Add as plain title")
                    tooltip: qsTr("An ordinary Dafin title with the text above, in a matching style. No animation.")
                    onClicked: root.addEditable()
                }

                ThemedButton {
                    variant: "primary"
                    visible: root.animates
                    glyph: Theme.icons.wand
                    text: qsTr("Add template title")
                    onClicked: root.addTemplate()
                }
            }
        }
    }
}
