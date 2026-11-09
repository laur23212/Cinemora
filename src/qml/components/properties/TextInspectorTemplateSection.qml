import QtQuick
import Drift

// Dafin: the title template this clip came from, its quick controls, and the three library
// actions kept apart on purpose:
//   • edits here change only this clip (live, one undo step each);
//   • Save as new template makes a new entry in My templates and never overwrites anything;
//   • Update "…" writes this look back into the user's own template, after asking.
Column {
    id: root
    property var inspector
    width: parent ? parent.width : implicitWidth
    spacing: Theme.spacingSm
    visible: inspector.hasTextStyle

    readonly property var info: {
        void root.inspector.textStyle
        void EditorState.selectedClipData
        void root.libraryTick
        return EditorState.textTemplateInfo(EditorState.selectedTrack, EditorState.selectedClip) || ({})
    }
    readonly property var controls: {
        void root.inspector.textStyle
        void EditorState.selectedClipData
        void root.libraryTick
        return EditorState.templateControls(EditorState.selectedTrack, EditorState.selectedClip) || []
    }
    readonly property bool hasTemplate: !!info.id && info.missing !== true
    readonly property var mainControls: controls.filter(c => !c.group)
    readonly property var groups: {
        const seen = []
        for (const c of controls)
            if (c.group && seen.indexOf(c.group) < 0)
                seen.push(c.group)
        return seen
    }

    property int libraryTick: 0
    Connections {
        target: EditorState
        function onUserTextPresetsChanged() { root.libraryTick++ }
        function onTextTemplatesChanged() { root.libraryTick++ }
    }

    // Header: which template, and where it lives.
    Row {
        width: parent.width
        spacing: Theme.spacingSm
        visible: root.hasTemplate || root.info.missing === true

        Text {
            width: parent.width - (resetAll.visible ? resetAll.width + parent.spacing : 0)
            anchors.verticalCenter: parent.verticalCenter
            text: root.info.missing === true ? qsTr("Template not installed")
                                             : qsTr("%1 · %2").arg(root.info.label || "").arg(root.info.originLabel || "")
            elide: Text.ElideRight
            color: root.info.missing === true ? Theme.warning : Theme.panelForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            font.weight: Font.DemiBold
        }
        IconButton {
            id: resetAll
            visible: root.hasTemplate
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.reset
            variant: "ghost"
            buttonSize: Theme.controlHeightSm
            iconSize: 14
            tooltip: qsTr("Reset to the template: its look and effects come back, your text stays")
            onClicked: EditorState.resetTextTemplate(EditorState.selectedTrack, EditorState.selectedClip)
        }
    }

    Text {
        visible: root.hasTemplate && !!root.info.description
        width: parent.width
        wrapMode: Text.WordWrap
        text: root.info.description || ""
        color: Theme.mutedForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    Repeater {
        model: root.mainControls
        delegate: TemplateControlField {
            required property var modelData
            width: root.width
            control: modelData
        }
    }

    Repeater {
        model: root.groups
        delegate: CollapsibleSection {
            id: groupSection
            required property string modelData
            width: root.width
            title: modelData
            expanded: false
            Repeater {
                model: root.controls.filter(c => c.group === groupSection.modelData)
                delegate: TemplateControlField {
                    required property var modelData
                    width: groupSection.width
                    control: modelData
                }
            }
        }
    }

    // The library actions, always within reach of the controls.
    ThemedButton {
        width: parent.width
        variant: "primary"
        glyph: Theme.icons.save
        text: qsTr("Save as new template…")
        tooltip: qsTr("Keep this look as a new template in My templates. Nothing is overwritten.")
        onClicked: root.inspector.openSaveStyle()
    }
    ThemedButton {
        visible: root.hasTemplate && root.info.isUser === true
        width: parent.width
        variant: "secondary"
        text: qsTr("Update “%1”…").arg(root.info.label || "")
        tooltip: qsTr("Write this look into your template. Titles already using it keep their own look.")
        onClicked: updateDialog.open()
    }

    ThemedDialog {
        id: updateDialog
        title: qsTr("Update template")
        acceptText: qsTr("Update")
        preferredWidth: Theme.dialogWidthSm
        onAccepted: EditorState.updateTextTemplate(EditorState.selectedTrack, EditorState.selectedClip)
        contentItem: Text {
            width: parent ? parent.width : 320
            wrapMode: Text.WordWrap
            text: qsTr("Replace the look of “%1” with this title's? Its name and category stay. Titles already made from it are not changed.")
                      .arg(root.info.label || "")
            color: Theme.panelForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
        }
    }
}
