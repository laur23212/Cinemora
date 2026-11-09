import QtQuick
import Drift

CollapsibleSection {
    id: root
    property var inspector
    width: parent.width
    title: qsTr("Preset")
    tooltip: qsTr("A whole text style — font, colour and effect — applied in one tap")
    collapsible: false
    showSeparator: false
    visible: inspector.hasTextStyle

    Text {
        width: parent.width
        wrapMode: Text.WordWrap
        text: qsTr("Font, colour and effect in one tap. Save your own to reuse it.")
        color: Theme.c.textInspectorPresetSection.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    TextStylePackPicker {
        width: parent.width
        packId: inspector.textStyle.packId || ""
        onPackPicked: id => EditorState.applyTextPreset(
                           EditorState.selectedTrack,
                           EditorState.selectedClip,
                           id)
    }

    ThemedButton {
        width: parent.width
        variant: "secondary"
        glyph: Theme.icons.save
        text: qsTr("Save as new template…")
        tooltip: qsTr("Keep this look (style and effects) as a new template in My templates")
        onClicked: inspector.openSaveStyle()
    }

    Row {
        width: parent.width
        spacing: Theme.spacingSm
        visible: inspector.clipKind === "subtitle"

        ThemedButton {
            width: (parent.width - parent.spacing) * 0.6
            variant: "secondary"
            glyph: Theme.icons.captions
            text: qsTr("Apply to all captions")
            tooltip: qsTr("Copy this style to every other caption on this track")
            onClicked: EditorState.applyTextStyleToCaptions(
                           EditorState.selectedTrack, EditorState.selectedClip, "track")
        }
        ThemedButton {
            width: (parent.width - parent.spacing) * 0.4
            variant: "secondary"
            text: qsTr("…every track")
            tooltip: qsTr("Copy this style to every caption in the project")
            onClicked: EditorState.applyTextStyleToCaptions(
                           EditorState.selectedTrack, EditorState.selectedClip, "project")
        }
    }

    ThemedButton {
        visible: inspector.clipKind === "subtitle"
        width: parent.width
        variant: "secondary"
        glyph: Theme.icons.audioLines
        text: qsTr("Generate voiceover")
        enabled: EditorState.tts.available
        tooltip: EditorState.tts.available
                 ? qsTr("Speak every caption with a generated voice")
                 : qsTr("Install the voice model from the add-on manager first")
        onClicked: inspector.openVoiceover()
    }

    Text {
        visible: inspector.clipKind === "subtitle" && !EditorState.tts.available
        width: parent.width
        wrapMode: Text.WordWrap
        text: qsTr("Install the AI engine and the voice model from the add-on manager to generate voiceovers.")
        color: Theme.c.textInspectorPresetSection.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }
}
