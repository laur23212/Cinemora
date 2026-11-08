import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin: the Template tab of a .mogrt title. Lists the template's own Essential Graphics controls
// (text, sliders, checkboxes, colours, positions) with the clip's values; each change is one undo
// step, and MOGRT Converter renders the new state in the background while the last render keeps
// playing. Controls MOGRT Converter cannot set (dropdowns, media) are shown, but not editable.
// State comes from EditorState.mogrt.clipState (MogrtTitleController).
Item {
    id: root

    property int clipDataRevision: 0
    readonly property var st: {
        void clipDataRevision
        void EditorState.mogrt.revision
        return EditorState.mogrt.clipState(EditorState.selectedTrack, EditorState.selectedClip)
    }
    readonly property var controls: st.controls || []
    readonly property string status: st.status || ""

    function setValue(id, value) {
        EditorState.mogrt.setValue(EditorState.selectedTrack, EditorState.selectedClip, id, value)
    }
    // A colour's shade as #rrggbb (a colour with alpha prints as #aarrggbb).
    function hexOf(c) {
        if (!c || c.r === undefined)
            return "#ffffff"
        const h = v => ("0" + Math.round(v * 255).toString(16)).slice(-2)
        return "#" + h(c.r) + h(c.g) + h(c.b)
    }
    function control(i) {
        return i >= 0 && i < root.controls.length ? root.controls[i] : ({})
    }

    height: contentCol.height
    implicitHeight: contentCol.height

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++ }
        function onTracksChanged() { root.clipDataRevision++ }
    }

    Column {
        id: contentCol
        width: root.width
        spacing: Theme.spacingLg

        // ----- what the render shows ------------------------------------------------------
        Column {
            width: parent.width
            spacing: Theme.spacingSm

            ThemedLabel {
                width: parent.width
                elide: Text.ElideRight
                text: root.st.name || qsTr("Template")
                SearchAnchor {
                    searchId: "mogrt-template"
                    title: qsTr("Template title")
                    keywords: "mogrt motion graphics template essential graphics"
                }
            }

            ThemedLabel {
                width: parent.width
                size: "sm"
                opacity: 0.8
                wrapMode: Text.WordWrap
                text: {
                    const parts = []
                    if (root.st.quality === "full")
                        parts.push(qsTr("Full quality"))
                    else if (root.st.quality === "preview")
                        parts.push(qsTr("Preview quality (half size); the export renders it in full"))
                    if (root.st.size)
                        parts.push(root.st.size)
                    return parts.join(" · ")
                }
                visible: text.length > 0
            }

            ThemedProgressBar {
                width: parent.width
                visible: !!root.st.rendering
                value: root.st.progress || 0
            }

            ThemedLabel {
                width: parent.width
                size: "sm"
                wrapMode: Text.WordWrap
                visible: text.length > 0
                color: root.status === "failed" || root.status === "missing" || root.status === "unsupported"
                       ? Theme.destructive : Theme.panelForeground
                text: root.st.message || ""
            }

            Row {
                spacing: Theme.spacingSm
                visible: root.status === "missing" || root.status === "needsConverter" || root.status === "failed"
                ThemedButton {
                    visible: root.status === "missing"
                    variant: "primary"
                    glyph: Theme.icons.folder
                    text: qsTr("Locate template…")
                    onClicked: {
                        const url = FileDialogs.openFile(qsTr("Locate %1").arg(root.st.name || qsTr("template")),
                                                         [qsTr("Motion Graphics Templates (*.mogrt)")])
                        if (url && url.toString() !== "")
                            EditorState.mogrt.locateTemplate(EditorState.selectedTrack, EditorState.selectedClip, url)
                    }
                }
                ThemedButton {
                    visible: root.status === "needsConverter"
                    variant: "secondary"
                    glyph: Theme.icons.externalLink
                    text: qsTr("Get MOGRT Converter")
                    onClicked: Qt.openUrlExternally(MogrtRenderer.downloadUrl)
                }
                ThemedButton {
                    visible: root.status === "needsConverter"
                    variant: "ghost"
                    glyph: Theme.icons.refresh
                    text: qsTr("Check again")
                    onClicked: MogrtRenderer.refresh()
                }
                ThemedButton {
                    visible: root.status === "failed"
                    variant: "secondary"
                    glyph: Theme.icons.refresh
                    text: qsTr("Try again")
                    onClicked: EditorState.mogrt.renderAgain(EditorState.selectedTrack, EditorState.selectedClip)
                }
            }

            // What MOGRT Converter reported it could not draw.
            Column {
                width: parent.width
                spacing: 2
                visible: (root.st.warnings || []).length > 0
                ThemedLabel {
                    width: parent.width
                    size: "sm"
                    wrapMode: Text.WordWrap
                    text: qsTr("Partly supported: MOGRT Converter left out what it can't draw yet, so this can look different from After Effects.")
                }
                Repeater {
                    model: root.st.warnings || []
                    delegate: ThemedLabel {
                        required property string modelData
                        width: contentCol.width
                        size: "sm"
                        opacity: 0.75
                        wrapMode: Text.WordWrap
                        text: "• " + modelData
                    }
                }
            }
        }

        // ----- the template's controls ----------------------------------------------------
        ThemedLabel {
            width: parent.width
            size: "sm"
            opacity: 0.8
            wrapMode: Text.WordWrap
            visible: root.controls.length === 0
            text: qsTr("This template has no editable controls.")
        }

        Column {
            width: parent.width
            spacing: Theme.spacingMd

            // A count, not the list: delegates must survive the state refreshing while you type.
            Repeater {
                model: root.controls.length
                delegate: Column {
                    id: row
                    required property int index
                    readonly property var c: root.control(index)
                    readonly property string type: c.type || ""
                    width: contentCol.width
                    spacing: 4

                    // Groups and sections head the controls under them.
                    Text {
                        visible: row.type === "group" || row.type === "section"
                        width: parent.width
                        topPadding: row.index > 0 ? Theme.spacingSm : 0
                        elide: Text.ElideRight
                        text: row.c.label || ""
                        color: Theme.panelForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                        font.weight: Font.DemiBold
                    }

                    Text {
                        visible: row.type !== "group" && row.type !== "section" && row.type !== "checkbox"
                        width: parent.width
                        elide: Text.ElideRight
                        text: (row.c.label || qsTr("Control %1").arg(row.index + 1)) + (row.c.changed ? "  •" : "")
                        color: Theme.c.pdfInspector.foregroundMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }

                    // text — committed when you leave the field, or after a short pause in typing
                    ThemedTextArea {
                        id: textArea
                        visible: row.type === "text"
                        width: parent.width
                        wrapMode: TextEdit.Wrap
                        readonly property string stored: row.type === "text" ? String(row.c.value || "") : ""
                        function commit() {
                            typingTimer.stop()
                            if (text !== stored)
                                root.setValue(row.c.id, text)
                        }
                        Component.onCompleted: text = stored
                        onStoredChanged: if (!activeFocus) text = stored
                        onTextChanged: if (activeFocus) typingTimer.restart()
                        onActiveFocusChanged: if (!activeFocus) commit()
                        Timer {
                            id: typingTimer
                            interval: 900
                            onTriggered: textArea.commit()
                        }
                    }

                    // slider / angle. Controls set their own value while used, which ends a
                    // plain binding, so the stored value is pushed in whenever it changes (undo).
                    Row {
                        visible: row.type === "slider" || row.type === "angle"
                        width: parent.width
                        spacing: 8
                        readonly property bool bounded: row.c.min !== undefined && row.c.max !== undefined
                                                        && Number(row.c.max) > Number(row.c.min)
                        readonly property real stored: Number(row.c.value) || 0
                        function sync() {
                            if (!slider.pressed)
                                slider.value = stored
                            if (!numberField.activeFocus)
                                numberField.value = stored
                        }
                        onStoredChanged: sync()
                        Component.onCompleted: sync()
                        ThemedSlider {
                            id: slider
                            visible: parent.bounded
                            width: parent.width - numberField.width - parent.spacing
                            anchors.verticalCenter: parent.verticalCenter
                            from: parent.bounded ? Number(row.c.min) : 0
                            to: parent.bounded ? Number(row.c.max) : 1
                            onMoved: numberField.value = value
                            // One undo step and one render per drag.
                            onPressedChanged: if (!pressed) root.setValue(row.c.id, value)
                        }
                        ThemedNumberField {
                            id: numberField
                            width: parent.bounded ? 92 : parent.width
                            from: parent.bounded ? Number(row.c.min) : -1e9
                            to: parent.bounded ? Number(row.c.max) : 1e9
                            decimals: 2
                            unit: row.type === "angle" ? "°" : ""
                            onEdited: v => root.setValue(row.c.id, v)
                        }
                    }

                    // checkbox
                    ThemedCheckBox {
                        id: checkBox
                        visible: row.type === "checkbox"
                        text: row.c.label || ""
                        readonly property bool stored: !!row.c.value
                        onStoredChanged: checked = stored
                        Component.onCompleted: checked = stored
                        onToggled: root.setValue(row.c.id, checked)
                    }

                    // color
                    ColorSwatchField {
                        visible: row.type === "color"
                        hex: row.type === "color" ? root.hexOf(row.c.value) : "#ffffff"
                        tooltip: qsTr("Choose %1").arg(row.c.label || qsTr("colour"))
                        onEdited: value => root.setValue(row.c.id, value)
                    }

                    // point / point3d / scale: one field per axis
                    Row {
                        visible: row.type === "point" || row.type === "point3d" || row.type === "scale"
                        width: parent.width
                        spacing: 8
                        readonly property var axes: Array.isArray(row.c.value) ? row.c.value : []
                        readonly property int count: row.type === "point3d" ? 3 : Math.max(2, Math.min(3, axes.length))
                        Repeater {
                            model: parent.visible ? parent.count : 0
                            delegate: ThemedNumberField {
                                id: axisField
                                required property int index
                                readonly property var axisRow: parent
                                readonly property real stored: Number(axisRow.axes[index]) || 0
                                width: (row.width - 8 * 2) / 3
                                decimals: 1
                                unit: index === 0 ? "X" : index === 1 ? "Y" : "Z"
                                onStoredChanged: if (!activeFocus) value = stored
                                Component.onCompleted: value = stored
                                onEdited: v => {
                                    const next = []
                                    for (let i = 0; i < axisRow.count; ++i)
                                        next.push(i === index ? v : Number(axisRow.axes[i]) || 0)
                                    root.setValue(row.c.id, next)
                                }
                            }
                        }
                    }

                    // what MOGRT Converter can't set
                    ThemedLabel {
                        visible: !row.c.editable && row.type !== "group" && row.type !== "section"
                        width: parent.width
                        size: "sm"
                        opacity: 0.7
                        wrapMode: Text.WordWrap
                        text: row.type === "dropdown" ? qsTr("Dropdown: uses the template's choice. MOGRT Converter can't change it yet.")
                            : row.type === "media" ? qsTr("Media replacement isn't supported yet.")
                            : qsTr("Not editable in Dafin yet.")
                    }
                }
            }
        }

        // ----- actions ----------------------------------------------------------------------
        Flow {
            width: parent.width
            spacing: Theme.spacingSm

            ThemedButton {
                visible: root.st.quality === "preview" && root.st.converterAvailable
                enabled: !(root.st.rendering && root.st.renderingQuality === "full")
                variant: "secondary"
                glyph: Theme.icons.sparkles
                text: qsTr("Render full quality")
                tooltip: qsTr("Exports do this on their own. Useful to judge the result at full size.")
                onClicked: EditorState.mogrt.renderFullQuality(EditorState.selectedTrack, EditorState.selectedClip)
            }
            ThemedButton {
                visible: !!root.st.converterAvailable && !root.st.templateMissing
                variant: "ghost"
                glyph: Theme.icons.refresh
                text: qsTr("Render again")
                tooltip: qsTr("Throws this title's renders away and makes them again, after installing a missing font for example.")
                onClicked: EditorState.mogrt.renderAgain(EditorState.selectedTrack, EditorState.selectedClip)
            }
            ThemedButton {
                visible: !!root.st.hasChanges
                variant: "ghost"
                glyph: Theme.icons.reset
                text: qsTr("Reset all")
                tooltip: qsTr("Back to the template's own values")
                onClicked: EditorState.mogrt.resetValues(EditorState.selectedTrack, EditorState.selectedClip)
            }
        }
    }
}
