import QtQuick
import Drift

// Dafin: one quick control of a title template, built from what the template declares
// ({id, label, type, min, max, step, unit, options, value, defaultValue, modified}). The
// control type picks the widget; every edit goes through EditorState.setTemplateControl so the
// binding (and undo) live in one place.
Column {
    id: root
    property var control: ({})
    readonly property string type: control.type || ""
    width: parent ? parent.width : implicitWidth
    spacing: 4

    function commit(value) {
        EditorState.setTemplateControl(EditorState.selectedTrack, EditorState.selectedClip, root.control.id, value)
    }

    function formatScalar(v) {
        const step = Number(root.control.step) || 0
        const decimals = step >= 1 || step === 0 && (root.control.max - root.control.min) > 20 ? 0
                       : step >= 0.1 ? 1 : 2
        const unit = root.control.unit ? " " + root.control.unit : ""
        return Number(v).toFixed(decimals) + unit
    }

    Row {
        width: parent.width
        spacing: 6
        visible: root.type !== "bool"

        Text {
            width: parent.width - (resetButton.visible ? resetButton.width + parent.spacing : 0)
                   - (valueLabel.visible ? valueLabel.width + parent.spacing : 0)
            anchors.verticalCenter: parent.verticalCenter
            text: root.control.label || ""
            elide: Text.ElideRight
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
        Text {
            id: valueLabel
            visible: root.type === "scalar"
            anchors.verticalCenter: parent.verticalCenter
            text: root.formatScalar(slider.value)
            color: Theme.mutedForeground
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSizeXs
        }
        IconButton {
            id: resetButton
            visible: root.control.modified === true
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.reset
            variant: "ghost"
            buttonSize: 18
            iconSize: 12
            tooltip: qsTr("Back to the template's value")
            onClicked: EditorState.resetTemplateControl(EditorState.selectedTrack, EditorState.selectedClip, root.control.id)
        }
    }

    // text
    ThemedTextField {
        id: textField
        visible: root.type === "text"
        width: parent.width
        text: root.type === "text" ? String(root.control.value || "") : ""
        onEditingFinished: if (text !== String(root.control.value || "")) root.commit(text)
    }

    // font
    FontPicker {
        visible: root.type === "font"
        width: parent.width
        family: root.type === "font" ? String(root.control.value || "") : ""
        onFamilyPicked: family => root.commit(family)
    }

    // color
    ColorSwatchField {
        visible: root.type === "color"
        hex: root.type === "color" ? String(root.control.value || "#ffffffff") : "#ffffffff"
        tooltip: root.control.label || qsTr("Choose colour")
        onEdited: value => root.commit(value)
    }

    // scalar
    ThemedSlider {
        id: slider
        visible: root.type === "scalar"
        lockWhilePlaying: true
        width: parent.width
        label: root.control.label || ""
        from: Number(root.control.min) || 0
        to: root.type === "scalar" ? Number(root.control.max) : 1
        stepSize: Number(root.control.step) || 0
        valueFormatter: function (v) { return root.formatScalar(v) }
        Binding on value {
            when: !slider.pressed && root.type === "scalar"
            value: Number(root.control.value) || 0
        }
        onMoved: {
            if (pressed)
                EditorState.previewSetTemplateControl(EditorState.selectedTrack, EditorState.selectedClip,
                                                      root.control.id, value)
            else
                root.commit(value)
        }
        onPressedChanged: {
            if (pressed)
                EditorState.beginPreviewDrag(qsTr("Edit template"))
            else
                EditorState.commitPreviewDrag()
        }
    }

    // bool
    Row {
        visible: root.type === "bool"
        width: parent.width
        spacing: 6
        ThemedSwitch {
            id: toggle
            width: parent.width - (boolReset.visible ? boolReset.width + parent.spacing : 0)
            text: root.control.label || ""
            tooltip: root.control.description || ""
            checked: root.type === "bool" && root.control.value === true
            onToggled: root.commit(checked)
        }
        IconButton {
            id: boolReset
            visible: root.control.modified === true
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.reset
            variant: "ghost"
            buttonSize: 18
            iconSize: 12
            tooltip: qsTr("Back to the template's value")
            onClicked: EditorState.resetTemplateControl(EditorState.selectedTrack, EditorState.selectedClip, root.control.id)
        }
    }

    // enum
    ThemedComboBox {
        visible: root.type === "enum"
        width: parent.width
        readonly property var options: root.control.options || []
        model: options.map(o => o.label)
        currentIndex: {
            for (let i = 0; i < options.length; ++i)
                if (options[i].value === root.control.value)
                    return i
            return -1
        }
        onActivated: index => root.commit(options[index].value)
    }
}
