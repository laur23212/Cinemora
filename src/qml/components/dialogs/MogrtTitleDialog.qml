import QtQuick
import QtQuick.Controls.Basic
import QtMultimedia
import Drift

// Dafin: adding a .mogrt template from the Titles panel. The template's text fields can be filled
// in first, then either:
//   • Add animated — MOGRT Converter (a separate free program, see MogrtRenderer) renders the
//     template with its After Effects animation into a transparent video, which goes on the
//     timeline above the video like a title. Templates made in Premiere Pro it cannot render.
//   • Add as editable title — the text as an ordinary Dafin title in a matching style, no
//     animation. Always available.
// A render keeps going if the dialog is closed; the clip still lands when it is done.
ThemedDialog {
    id: root

    property var item: ({})
    readonly property var extra: item && item.extra ? item.extra : ({})
    readonly property string mogrtPath: item && item.downloadUrl ? MogrtRenderer.pathFor(item.downloadUrl) : ""
    readonly property bool premiereMade: (extra.authorApp || "") === "ppro"
    readonly property bool canAnimate: MogrtRenderer.available && !premiereMade && mogrtPath.length > 0
    property var fields: []
    property string jobKey: ""
    readonly property string jobState: MogrtRenderer.revision >= 0 && jobKey.length > 0 ? MogrtRenderer.state(jobKey) : ""
    readonly property bool rendering: jobState === "rendering"

    title: item && item.title ? item.title : qsTr("Title template")
    preferredWidth: Theme.dialogWidthMd
    showFooter: false
    acceptOnReturn: false
    closePolicy: rendering ? Popup.CloseOnEscape : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)

    function openFor(templateItem) {
        if (root.rendering) {
            Toasts.info(qsTr("Still rendering %1…").arg(root.title))
            root.open()
            return
        }
        root.item = templateItem
        const texts = (templateItem.extra && templateItem.extra.texts) || []
        const list = []
        for (let i = 0; i < texts.length; ++i)
            list.push({ id: texts[i].id || "", label: texts[i].label || "", text: texts[i].text || "" })
        if (list.length === 0)
            list.push({ id: "", label: qsTr("Text"), text: (templateItem.extra && templateItem.extra.text) || templateItem.title || "" })
        root.fields = list
        lengthField.value = Math.max(0.5, Number((templateItem.extra && templateItem.extra.duration) || 5))
        root.jobKey = ""
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

    function addAnimated() {
        root.jobKey = MogrtRenderer.render(root.mogrtPath, root.fields, lengthField.value, root.item.title || "")
    }

    Connections {
        target: MogrtRenderer
        function onRendered(key, outputPath, name, warnings) {
            if (key !== root.jobKey)
                return
            const url = MogrtRenderer.urlFor(outputPath)
            MediaImport.importUrls([url], false, function (added) {
                if (added <= 0) {
                    Toasts.error(qsTr("The rendered template could not be opened."))
                    return
                }
                EditorState.addClipFromAsset(AssetLibrary.count - added)
                if (warnings.length > 0)
                    Toasts.info(qsTr("%1 added. MOGRT Converter skipped %n part(s) it can't draw yet.", "", warnings.length).arg(name))
                else
                    Toasts.success(qsTr("%1 added with its animation.").arg(name))
            })
            root.jobKey = ""
            root.close()
        }
        function onFailed(key, message) {
            if (key === root.jobKey && !root.visible)
                Toasts.error(message)
        }
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
                    enabled: !root.rendering
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
                enabled: !root.rendering
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
            text: root.premiereMade
                  ? qsTr("This template was made in Premiere Pro. Its animation can't be drawn outside Adobe's apps, so it is added as an editable Dafin title with your text.")
                  : MogrtRenderer.available
                    ? qsTr("Add animated renders the template's After Effects animation with MOGRT Converter into a transparent clip. Effects it can't draw yet are left out. To change the text later, add it again.")
                    : qsTr("To get the template's animation, install the free MOGRT Converter and press Check again. Without it, Dafin adds an editable title with your text.")
        }

        Row {
            visible: !MogrtRenderer.available && !root.premiereMade
            spacing: Theme.spacingSm
            ThemedButton {
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

        Column {
            width: parent.width
            spacing: Theme.spacingXs
            visible: root.jobKey.length > 0 && root.jobState !== ""
            ThemedProgressBar {
                width: parent.width
                visible: root.rendering
                value: MogrtRenderer.revision >= 0 ? MogrtRenderer.progress(root.jobKey) : 0
            }
            ThemedLabel {
                width: parent.width
                size: "sm"
                wrapMode: Text.WordWrap
                text: MogrtRenderer.revision >= 0 && root.jobKey.length > 0 ? MogrtRenderer.message(root.jobKey) : ""
            }
        }

        Item {
            width: parent.width
            height: animateButton.height

            ThemedButton {
                anchors.left: parent.left
                variant: "ghost"
                text: root.rendering ? qsTr("Cancel") : qsTr("Close")
                onClicked: {
                    if (root.rendering) {
                        MogrtRenderer.cancel(root.jobKey)
                        root.jobKey = ""
                    } else {
                        root.close()
                    }
                }
            }

            Row {
                anchors.right: parent.right
                spacing: Theme.spacingLg

                ThemedButton {
                    variant: root.canAnimate ? "secondary" : "primary"
                    enabled: !root.rendering
                    text: qsTr("Add as editable title")
                    onClicked: root.addEditable()
                }

                ThemedButton {
                    id: animateButton
                    variant: "primary"
                    visible: !root.premiereMade
                    enabled: root.canAnimate && !root.rendering
                    glyph: Theme.icons.wand
                    text: root.rendering ? qsTr("Rendering…") : qsTr("Add animated")
                    tooltip: MogrtRenderer.available ? qsTr("Renders the animation with %1").arg(MogrtRenderer.executable)
                                                     : qsTr("Needs MOGRT Converter")
                    onClicked: root.addAnimated()
                }
            }
        }
    }
}
