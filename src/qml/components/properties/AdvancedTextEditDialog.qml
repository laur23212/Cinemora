pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin: Filmora's Advanced Text Edit. A big window over the editor for one text clip: a toolbar
// for the type (font, size, bold, italic, underline, case, alignment, line and letter spacing —
// hover a number and turn the wheel), a live picture of the frame with its own little player
// over the clip, and on the right the clip's text with Fill, Outline, Shadow, Glow, Background and
// Shape switches (Customize), the full look and layer editor (Styles) and the In / Out / Loop
// animations (Animation).
//
// Everything applies as it is changed, so the picture is always the real render. Cancel puts the
// clip back the way it was (every step since the window opened is undone); Apply keeps it, and
// one Undo afterwards still takes back a single change.
ThemedDialog {
    id: root

    title: qsTr("Advanced Text Edit")
    acceptText: qsTr("Apply")
    rejectText: qsTr("Cancel")
    // Enter types a new line in the text box.
    acceptOnReturn: false
    preferredWidth: 1180
    closePolicy: Popup.CloseOnEscape

    property int track: -1
    property int clip: -1
    property int undoStart: 0
    property real playheadBefore: 0
    property bool wasPlaying: false
    property string tab: "customize"

    property int revision: 0
    readonly property var clipData: {
        void root.revision
        return EditorState.selectedClipData || ({})
    }
    readonly property bool ready: root.visible && root.clipData.kind === "text" && !!root.clipData.textStyle
    readonly property var textStyle: root.ready ? root.clipData.textStyle : ({})
    readonly property var layers: root.textStyle.layers || []

    readonly property var fontInfo: {
        void root.revision
        const catalog = EditorState.fontCatalog()
        for (let i = 0; i < catalog.length; ++i) {
            if (catalog[i].family === root.textStyle.fontFamily)
                return catalog[i]
        }
        return null
    }
    readonly property var weights: root.fontInfo ? root.fontInfo.weights : [400, 700]
    readonly property bool isBold: (Number(root.textStyle.fontWeight) || 400) >= 600

    readonly property var caseCycle: ["none", "upper", "lower", "title"]
    readonly property var caseNames: ({ "none": qsTr("As typed"), "upper": qsTr("UPPERCASE"),
                                        "lower": qsTr("lowercase"), "title": qsTr("Title Case"),
                                        "sentence": qsTr("Sentence case") })

    readonly property var clipRange: {
        void root.revision
        return root.track >= 0 ? EditorState.clipTimelineRange(root.track, root.clip) : ({})
    }
    readonly property real clipStart: Number(root.clipRange.startSeconds) || 0
    readonly property real clipEnd: Math.max(root.clipStart + 0.04, Number(root.clipRange.endSeconds) || 0)

    // ----- opening and closing -------------------------------------------------------------------

    function openFor(trackIndex, clipIndex) {
        if (trackIndex < 0 || clipIndex < 0)
            return
        if (EditorState.selectedTrack !== trackIndex || EditorState.selectedClip !== clipIndex)
            EditorState.selectClip(trackIndex, clipIndex)
        root.track = trackIndex
        root.clip = clipIndex
        root.tab = "customize"
        root.wasPlaying = EditorState.playing
        EditorState.playing = false
        root.undoStart = EditorState.undoIndex
        root.playheadBefore = EditorState.playheadSeconds
        root.revision++
        // Show the title, not the empty frame before its entrance: a moment in, or the middle of
        // a short clip.
        const range = EditorState.clipTimelineRange(trackIndex, clipIndex)
        const start = Number(range.startSeconds) || 0
        const end = Number(range.endSeconds) || start
        const now = EditorState.playheadSeconds
        if (now < start || now >= end)
            EditorState.playheadSeconds = start + Math.min(1.0, (end - start) / 2)
        textBox.text = root.clipData.textContent || ""
        root.open()
    }

    // Set once Apply or Cancel has decided; a close any other way counts as Cancel.
    property bool decided: false

    function revert() {
        textCommit.stop()
        EditorState.playing = false
        // Everything done in here, and nothing before it.
        let guard = 0
        while (EditorState.undoIndex > root.undoStart && guard++ < 1000)
            EditorState.undo()
        EditorState.playheadSeconds = root.playheadBefore
    }

    onOpened: root.decided = false
    onAccepted: {
        root.decided = true
        root.flushText()
        EditorState.playing = false
    }
    onRejected: {
        root.decided = true
        root.revert()
    }
    // closed can come before accepted/rejected (Dialog closes first, then reports), so the
    // fallback waits for the rest of the call to finish.
    onClosed: Qt.callLater(function() {
        if (!root.decided)
            root.revert()
        root.decided = true
    })

    Connections {
        target: EditorState
        function onSelectedClipDataChanged() { root.revision++ }
        function onSelectionChanged() { root.revision++ }
        function onTracksChanged() { root.revision++ }
    }

    // ----- writing -----------------------------------------------------------------------------

    function setStyle(patch) {
        EditorState.setTextStyle(root.track, root.clip, patch)
    }
    function setStyleKey(key, value) {
        const patch = {}
        patch[key] = value
        root.setStyle(patch)
    }

    // The fill the colour swatch edits is the front-most one, as in the Properties panel.
    function frontLayer(kind) {
        let fallback = null
        for (let i = root.layers.length - 1; i >= 0; --i) {
            const l = root.layers[i]
            if (l.kind !== kind)
                continue
            if (l.enabled !== false)
                return l
            if (!fallback)
                fallback = l
        }
        return fallback
    }
    function layerOn(layer) { return !!layer && layer.enabled !== false }

    function setLayer(layer, patch) {
        if (layer)
            EditorState.setStyleLayer(root.track, root.clip, layer.id, patch)
    }
    function previewLayer(layer, patch) {
        if (layer)
            EditorState.previewSetStyleLayer(root.track, root.clip, layer.id, patch)
    }
    // A switch that has no layer behind it yet makes one with the editor's defaults.
    function switchLayer(kind, on) {
        const layer = root.frontLayer(kind)
        if (layer) {
            root.setLayer(layer, { "enabled": on })
            return
        }
        if (on)
            EditorState.addStyleLayer(root.track, root.clip, kind)
    }

    function setBold(on) {
        const w = root.weights.slice().sort((a, b) => a - b)
        let pick = on ? w[w.length - 1] : w[0]
        if (on) {
            for (let i = 0; i < w.length; ++i) {
                if (w[i] >= 700) { pick = w[i]; break }
            }
        } else {
            for (let i = w.length - 1; i >= 0; --i) {
                if (w[i] <= 400) { pick = w[i]; break }
            }
        }
        root.setStyleKey("fontWeight", pick)
    }

    function nextCase() {
        const i = root.caseCycle.indexOf(root.textStyle.textCase || "none")
        root.setStyleKey("textCase", root.caseCycle[(i + 1) % root.caseCycle.length])
    }

    function flushText() {
        if (!textCommit.running)
            return
        textCommit.stop()
        if (textBox.text !== (root.clipData.textContent || ""))
            EditorState.commitTextEdit(root.track, root.clip, textBox.text)
    }

    function formatTime(seconds) {
        const s = Math.max(0, seconds)
        const m = Math.floor(s / 60)
        const rest = s - m * 60
        return (m < 10 ? "0" : "") + m + ":" + (rest < 10 ? "0" : "") + rest.toFixed(2)
    }

    Timer {
        id: textCommit
        interval: 350
        onTriggered: EditorState.commitTextEdit(root.track, root.clip, textBox.text)
    }

    // The little player loops over the clip instead of running on into the rest of the project.
    Timer {
        interval: 40
        repeat: true
        running: root.visible && EditorState.playing
        onTriggered: {
            if (EditorState.playheadSeconds >= root.clipEnd - 0.02) {
                EditorState.playing = false
                EditorState.playheadSeconds = root.clipStart
            }
        }
    }

    // ----- small parts ---------------------------------------------------------------------------

    // A number in the toolbar: type it, drag on it, or just hover it and turn the wheel.
    component WheelNumber: Item {
        id: wn
        property string glyph: ""
        property string caption: ""
        property string tip: ""
        property real value: 0
        property real from: 0
        property real to: 100
        property real step: 1
        property int decimals: 0
        signal edited(real value)

        width: (wn.glyph.length > 0 ? 22 : 0) + 64
        height: Theme.controlHeightSm

        IconGlyph {
            id: wnGlyph
            visible: wn.glyph.length > 0
            glyph: wn.glyph
            iconSize: 15
            iconColor: Theme.mutedForeground
            anchors.verticalCenter: parent.verticalCenter
        }
        ThemedNumberField {
            id: wnField
            x: wn.glyph.length > 0 ? 22 : 0
            width: 64
            height: parent.height
            from: wn.from
            to: wn.to
            step: wn.step
            decimals: wn.decimals
            Binding on value {
                when: !wnField.activeFocus
                value: wn.value
            }
            onEdited: v => wn.edited(v)
        }
        WheelHandler {
            // The field takes the wheel itself only while it is being typed in.
            enabled: !wnField.activeFocus
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            property real accum: 0
            onWheel: (event) => {
                accum += event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x
                const notches = Math.trunc(accum / 120)
                if (notches === 0)
                    return
                accum -= notches * 120
                const fine = (event.modifiers & Qt.ShiftModifier) ? 0.1 : 1
                const next = Math.max(wn.from, Math.min(wn.to, wn.value + notches * wn.step * fine))
                wn.edited(wn.decimals > 0 ? Number(next.toFixed(wn.decimals)) : Math.round(next))
            }
        }
        HoverHandler { id: wnHover }
        ThemedToolTip {
            text: wn.tip
            visible: wn.tip.length > 0 && wnHover.hovered && !wnField.activeFocus
        }
    }

    component ToolToggle: ThemedToggleButton {
        width: 30
        toggleHeight: Theme.controlHeightSm
    }

    component ToolDivider: Rectangle {
        width: 1
        height: Theme.controlHeightSm - 8
        y: 4
        color: Theme.panelBorder
    }

    // A labelled slider row. While dragging it previews (one undo step per drag); a click or the
    // keyboard commits straight away.
    component ValueRow: Item {
        id: vr
        property string label: ""
        property real from: 0
        property real to: 1
        property real stepSize: 0.01
        property real value: 0
        property int decimals: 0
        property string unit: ""
        property real displayScale: 1
        signal dragStarted()
        signal live(real value)
        signal dragEnded()
        signal commit(real value)

        width: parent ? parent.width : 200
        height: 30

        Text {
            id: vrLabel
            width: 78
            anchors.verticalCenter: parent.verticalCenter
            text: vr.label
            elide: Text.ElideRight
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
        ThemedSlider {
            id: vrSlider
            x: vrLabel.width + 6
            width: parent.width - x - vrValue.width - 6
            anchors.verticalCenter: parent.verticalCenter
            from: vr.from
            to: vr.to
            stepSize: vr.stepSize
            showValueTooltip: false
            Binding on value {
                when: !vrSlider.pressed
                value: vr.value
            }
            onMoved: {
                if (pressed)
                    vr.live(value)
                else
                    vr.commit(value)
            }
            onPressedChanged: {
                if (pressed)
                    vr.dragStarted()
                else
                    vr.dragEnded()
            }
        }
        Text {
            id: vrValue
            width: 46
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            horizontalAlignment: Text.AlignRight
            text: (vrSlider.value * vr.displayScale).toFixed(vr.decimals) + vr.unit
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }

    // A layer's slider: previews while dragged, as the Properties panel's do.
    // (Inline components cannot see this file's ids, so the dialog is handed in.)
    component LayerRow: ValueRow {
        id: lr
        property var dlg: null
        property var layer: null
        property string field: ""
        value: lr.layer ? (Number(lr.layer[lr.field]) || 0) : 0
        enabled: !!lr.layer && lr.layer.enabled !== false
        onDragStarted: EditorState.beginPreviewDrag(qsTr("Adjust %1").arg(lr.label.toLowerCase()))
        onLive: v => { const p = {}; p[lr.field] = v; lr.dlg.previewLayer(lr.layer, p) }
        onDragEnded: EditorState.commitPreviewDrag()
        onCommit: v => { const p = {}; p[lr.field] = v; lr.dlg.setLayer(lr.layer, p) }
    }

    // A text style value: one undo step per drag.
    component StyleRow: ValueRow {
        id: sr
        property var dlg: null
        property string key: ""
        value: sr.dlg ? (Number(sr.dlg.textStyle[sr.key]) || 0) : 0
        onDragStarted: EditorState.beginEditGroup()
        onLive: v => sr.dlg.setStyleKey(sr.key, v)
        onDragEnded: EditorState.endEditGroup(qsTr("Adjust %1").arg(sr.label.toLowerCase()))
        onCommit: v => sr.dlg.setStyleKey(sr.key, v)
    }

    // Filmora's section: a switch, a title, and its settings under it while it is on.
    component Section: Column {
        id: sec
        property string title: ""
        property bool isOn: false
        property bool switchable: true
        property bool open: true
        default property alias content: secBody.data
        signal switched(bool checked)

        width: parent ? parent.width : 300
        spacing: 6

        Item {
            width: parent.width
            height: 28

            ThemedSwitch {
                id: secSwitch
                visible: sec.switchable
                anchors.verticalCenter: parent.verticalCenter
                // Follows the clip again after a toggle (a click alone would cut a plain binding).
                Binding on checked {
                    value: sec.isOn
                }
                onToggled: sec.switched(checked)
            }
            Text {
                x: sec.switchable ? secSwitch.width + 6 : 0
                anchors.verticalCenter: parent.verticalCenter
                text: sec.title
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                font.weight: Font.DemiBold
            }
            IconGlyph {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                glyph: sec.open ? Theme.icons.chevronUp : Theme.icons.chevronDown
                iconSize: 14
                iconColor: Theme.mutedForeground
            }
            TapHandler {
                // The switch handles its own taps; the rest of the header folds the section.
                onTapped: (point) => {
                    if (!sec.switchable || point.position.x > secSwitch.width + 4)
                        sec.open = !sec.open
                }
            }
        }
        Column {
            id: secBody
            width: parent.width
            spacing: 4
            visible: sec.open
            opacity: !sec.switchable || sec.isOn ? 1 : 0.45
            enabled: !sec.switchable || sec.isOn
        }
        Rectangle {
            width: parent.width
            height: 1
            color: Theme.panelBorder
            opacity: 0.6
        }
    }

    component ColorRow: Item {
        id: cr
        property string label: qsTr("Colour")
        property string hex: "#ffffffff"
        signal picked(string value)
        width: parent ? parent.width : 200
        height: 32
        Text {
            width: 78
            anchors.verticalCenter: parent.verticalCenter
            text: cr.label
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
        ColorSwatchField {
            x: 84
            width: Math.min(180, parent.width - 84)
            anchors.verticalCenter: parent.verticalCenter
            hex: cr.hex
            onEdited: value => cr.picked(value)
        }
    }

    // ----- the window ----------------------------------------------------------------------------

    contentItem: Item {
        implicitWidth: 1120
        implicitHeight: Math.min(660, root.availableContentHeight)

        // Left: toolbar, picture, player.
        Item {
            id: left
            width: Math.round(parent.width * 0.6)
            height: parent.height

            Flow {
                id: toolbar
                width: parent.width
                spacing: 6

                FontPicker {
                    width: 190
                    family: root.textStyle.fontFamily || ""
                    onFamilyPicked: family => root.setStyleKey("fontFamily", family)
                }
                WheelNumber {
                    tip: qsTr("Size — hover and scroll to change it")
                    from: 4
                    to: 1000
                    value: Number(root.textStyle.pixelSize) || 64
                    onEdited: v => root.setStyleKey("pixelSize", v)
                }
                ToolDivider { }
                ToolToggle {
                    glyph: Theme.icons.bold
                    tooltip: qsTr("Bold")
                    checked: root.isBold
                    onClicked: root.setBold(!root.isBold)
                }
                ToolToggle {
                    glyph: Theme.icons.italic
                    tooltip: root.fontInfo && !root.fontInfo.hasItalic
                             ? qsTr("Italic (this font has no italic; it is slanted)") : qsTr("Italic")
                    checked: root.textStyle.italic === true
                    onClicked: root.setStyleKey("italic", !(root.textStyle.italic === true))
                }
                ToolToggle {
                    glyph: Theme.icons.underline
                    tooltip: qsTr("Underline")
                    checked: root.textStyle.underlineEnabled === true
                    onClicked: root.setStyleKey("underlineEnabled", !(root.textStyle.underlineEnabled === true))
                }
                ToolToggle {
                    glyph: Theme.icons.caseSensitive
                    tooltip: qsTr("Letter case: %1 — click for the next").arg(
                                 root.caseNames[root.textStyle.textCase || "none"] || "")
                    checked: (root.textStyle.textCase || "none") !== "none"
                    onClicked: root.nextCase()
                }
                ToolDivider { }
                Repeater {
                    model: [
                        { value: "left",   glyph: Theme.icons.alignLeft,   label: qsTr("Align left") },
                        { value: "center", glyph: Theme.icons.alignCenter, label: qsTr("Align centre") },
                        { value: "right",  glyph: Theme.icons.alignRight,  label: qsTr("Align right") }
                    ]
                    delegate: ToolToggle {
                        required property var modelData
                        glyph: modelData.glyph
                        tooltip: modelData.label
                        checked: root.textStyle.align === modelData.value
                        onClicked: root.setStyleKey("align", modelData.value)
                    }
                }
                ToolDivider { }
                WheelNumber {
                    glyph: Theme.icons.chevronsUpDown
                    tip: qsTr("Line spacing — hover and scroll to change it")
                    from: 0.5
                    to: 4
                    step: 0.05
                    decimals: 2
                    value: Number(root.textStyle.lineHeight) || 1.2
                    onEdited: v => root.setStyleKey("lineHeight", v)
                }
                WheelNumber {
                    glyph: Theme.icons.moveHorizontal
                    tip: qsTr("Letter spacing — hover and scroll to bring the letters closer or further apart (Shift for fine steps)")
                    from: -100
                    to: 200
                    step: 1
                    decimals: 1
                    value: Number(root.textStyle.letterSpacing) || 0
                    onEdited: v => root.setStyleKey("letterSpacing", v)
                }
            }

            Rectangle {
                id: stage
                y: toolbar.height + 8
                width: parent.width
                height: parent.height - y - player.height - 8
                color: "black"
                radius: Theme.radiusSm
                clip: true

                // The same frame the main preview shows, drawn again here. Only while the window
                // is open, and never the one that paces playback to the display (the main preview
                // does that; two would count every refresh twice).
                Loader {
                    anchors.fill: parent
                    active: root.visible
                    sourceComponent: Component {
                        PreviewItem {
                            stream: 0
                            pacesDisplay: false
                            playback: EditorState.playback
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: !root.ready
                    text: qsTr("Select a text clip")
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                }
            }

            Row {
                id: player
                anchors.bottom: parent.bottom
                width: parent.width
                height: 32
                spacing: 8

                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: EditorState.playing ? Theme.icons.pause : Theme.icons.play
                    tooltip: EditorState.playing ? qsTr("Pause") : qsTr("Play the title")
                    onClicked: {
                        if (!EditorState.playing && EditorState.playheadSeconds >= root.clipEnd - 0.05)
                            EditorState.playheadSeconds = root.clipStart
                        if (EditorState.playheadSeconds < root.clipStart)
                            EditorState.playheadSeconds = root.clipStart
                        EditorState.playing = !EditorState.playing
                    }
                }
                ThemedSlider {
                    id: scrub
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 40 - timeLabel.width - 16
                    from: root.clipStart
                    to: root.clipEnd
                    showValueTooltip: false
                    Binding on value {
                        when: !scrub.pressed
                        value: Math.max(root.clipStart, Math.min(root.clipEnd, EditorState.playheadSeconds))
                    }
                    onMoved: EditorState.playheadSeconds = value
                }
                Text {
                    id: timeLabel
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.formatTime(EditorState.playheadSeconds - root.clipStart) + " / "
                          + root.formatTime(root.clipEnd - root.clipStart)
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }
        }

        // Right: tabs.
        Item {
            id: right
            x: left.width + 14
            width: parent.width - x
            height: parent.height

            Row {
                id: tabs
                width: parent.width
                spacing: Theme.spacingSm
                Repeater {
                    model: [
                        { "id": "customize", "label": qsTr("Customize") },
                        { "id": "styles", "label": qsTr("Styles") },
                        { "id": "animation", "label": qsTr("Animation") }
                    ]
                    delegate: ThemedToggleButton {
                        required property var modelData
                        width: (tabs.width - tabs.spacing * 2) / 3
                        text: modelData.label
                        checked: root.tab === modelData.id
                        onClicked: root.tab = modelData.id
                    }
                }
            }

            Flickable {
                id: pane
                y: tabs.height + 8
                width: parent.width
                height: parent.height - y
                clip: true
                contentWidth: width
                contentHeight: paneColumn.height + 12
                boundsBehavior: Flickable.StopAtBounds
                // ThemedSlider holds the pane still while it is dragged.
                property int dragLocks: 0
                interactive: dragLocks === 0
                ScrollBar.vertical: AppScrollBar { }

                Column {
                    id: paneColumn
                    width: pane.width - 12
                    spacing: 10

                    // ---- Customize ----
                    Column {
                        width: parent.width
                        spacing: 10
                        visible: root.tab === "customize" && root.ready

                        Section {
                            title: qsTr("Input text")
                            switchable: false

                            ThemedTextArea {
                                id: textBox
                                width: parent.width
                                height: 92
                                wrapMode: TextEdit.Wrap
                                placeholderText: qsTr("Type your title")
                                onTextChanged: {
                                    if (activeFocus)
                                        textCommit.restart()
                                }
                                onActiveFocusChanged: {
                                    if (!activeFocus)
                                        root.flushText()
                                }
                            }
                            Text {
                                width: parent.width
                                horizontalAlignment: Text.AlignRight
                                text: textBox.text.length + "/1500"
                                color: textBox.text.length > 1500 ? "#e5484d" : Theme.mutedForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                            }
                        }

                        Section {
                            id: fillSection
                            readonly property var layer: root.frontLayer("fill")
                            title: qsTr("Fill")
                            isOn: root.layerOn(layer)
                            onSwitched: checked => root.switchLayer("fill", checked)

                            ColorRow {
                                hex: fillSection.layer && fillSection.layer.paint
                                     ? (fillSection.layer.paint.color || "#ffffffff") : "#ffffffff"
                                onPicked: value => root.setLayer(fillSection.layer, { "paint": { "kind": "solid", "color": value } })
                            }
                            LayerRow { dlg: root; layer: fillSection.layer; field: "opacity"; label: qsTr("Opacity"); displayScale: 100; unit: "%" }
                            LayerRow { dlg: root; layer: fillSection.layer; field: "blur"; label: qsTr("Blur"); to: 40; stepSize: 0.5; decimals: 1 }
                        }

                        Section {
                            id: outlineSection
                            readonly property var layer: root.frontLayer("stroke")
                            title: qsTr("Outline")
                            isOn: root.layerOn(layer)
                            onSwitched: checked => root.switchLayer("stroke", checked)

                            ColorRow {
                                hex: outlineSection.layer && outlineSection.layer.paint
                                     ? (outlineSection.layer.paint.color || "#ff000000") : "#ff000000"
                                onPicked: value => root.setLayer(outlineSection.layer, { "paint": { "kind": "solid", "color": value } })
                            }
                            LayerRow { dlg: root; layer: outlineSection.layer; field: "width"; label: qsTr("Width"); to: 40; stepSize: 0.5; decimals: 1; unit: " px" }
                            LayerRow { dlg: root; layer: outlineSection.layer; field: "opacity"; label: qsTr("Opacity"); displayScale: 100; unit: "%" }
                            LayerRow { dlg: root; layer: outlineSection.layer; field: "blur"; label: qsTr("Blur"); to: 30; stepSize: 0.5; decimals: 1 }
                        }

                        Section {
                            id: shadowSection
                            readonly property var layer: root.frontLayer("shadow")
                            readonly property real ox: shadowSection.layer ? (Number(shadowSection.layer.offsetX) || 0) : 0
                            readonly property real oy: shadowSection.layer ? (Number(shadowSection.layer.offsetY) || 0) : 0
                            readonly property real distance: Math.sqrt(ox * ox + oy * oy)
                            // 0° points right, 90° down, as the offset is drawn.
                            readonly property real angle: {
                                const a = Math.atan2(oy, ox) * 180 / Math.PI
                                return a < 0 ? a + 360 : a
                            }
                            function offsets(distance, angle) {
                                const r = angle * Math.PI / 180
                                return { "offsetX": distance * Math.cos(r), "offsetY": distance * Math.sin(r) }
                            }
                            title: qsTr("Shadow")
                            isOn: root.layerOn(layer)
                            onSwitched: checked => root.switchLayer("shadow", checked)

                            ColorRow {
                                hex: shadowSection.layer && shadowSection.layer.paint
                                     ? (shadowSection.layer.paint.color || "#ff000000") : "#ff000000"
                                onPicked: value => root.setLayer(shadowSection.layer, { "paint": { "kind": "solid", "color": value } })
                            }
                            LayerRow { dlg: root; layer: shadowSection.layer; field: "opacity"; label: qsTr("Opacity"); displayScale: 100; unit: "%" }
                            LayerRow { dlg: root; layer: shadowSection.layer; field: "blur"; label: qsTr("Blur"); to: 60; stepSize: 0.5; decimals: 1 }
                            ValueRow {
                                label: qsTr("Distance")
                                to: 80
                                stepSize: 0.5
                                decimals: 1
                                value: shadowSection.distance
                                enabled: shadowSection.isOn
                                onDragStarted: EditorState.beginPreviewDrag(qsTr("Adjust shadow"))
                                onLive: v => root.previewLayer(shadowSection.layer, shadowSection.offsets(v, shadowSection.angle))
                                onDragEnded: EditorState.commitPreviewDrag()
                                onCommit: v => root.setLayer(shadowSection.layer, shadowSection.offsets(v, shadowSection.angle))
                            }
                            ValueRow {
                                label: qsTr("Angle")
                                to: 360
                                stepSize: 1
                                unit: "°"
                                // A shadow with no offset has no direction; keep the usual one.
                                value: shadowSection.distance > 0.01 ? shadowSection.angle : 90
                                enabled: shadowSection.isOn
                                onDragStarted: EditorState.beginPreviewDrag(qsTr("Adjust shadow"))
                                onLive: v => root.previewLayer(shadowSection.layer, shadowSection.offsets(Math.max(shadowSection.distance, 4), v))
                                onDragEnded: EditorState.commitPreviewDrag()
                                onCommit: v => root.setLayer(shadowSection.layer, shadowSection.offsets(Math.max(shadowSection.distance, 4), v))
                            }
                        }

                        Section {
                            id: glowSection
                            readonly property var layer: root.frontLayer("glow")
                            title: qsTr("Glow")
                            isOn: root.layerOn(layer)
                            open: isOn
                            onSwitched: checked => root.switchLayer("glow", checked)

                            ColorRow {
                                hex: glowSection.layer && glowSection.layer.paint
                                     ? (glowSection.layer.paint.color || "#ffffffff") : "#ffffffff"
                                onPicked: value => root.setLayer(glowSection.layer, { "paint": { "kind": "solid", "color": value } })
                            }
                            LayerRow { dlg: root; layer: glowSection.layer; field: "blur"; label: qsTr("Size"); to: 80; stepSize: 0.5; decimals: 1 }
                            LayerRow { dlg: root; layer: glowSection.layer; field: "opacity"; label: qsTr("Opacity"); displayScale: 100; unit: "%" }
                        }

                        Section {
                            id: backgroundSection
                            title: qsTr("Background")
                            isOn: root.textStyle.boxEnabled === true
                            open: isOn
                            onSwitched: checked => root.setStyleKey("boxEnabled", checked)

                            ColorRow {
                                hex: root.textStyle.boxColor || "#80000000"
                                onPicked: value => root.setStyleKey("boxColor", value)
                            }
                            StyleRow { dlg: root; key: "boxPadding"; label: qsTr("Padding"); to: 120; stepSize: 1; unit: " px" }
                            StyleRow { dlg: root; key: "boxRadius"; label: qsTr("Corners"); to: 120; stepSize: 1; unit: " px" }
                        }

                        Section {
                            title: qsTr("Shape")
                            switchable: false
                            open: false

                            StyleRow { dlg: root; key: "pathBend"; label: qsTr("Bend"); from: -100; to: 100; stepSize: 1 }
                            Text {
                                width: parent.width
                                wrapMode: Text.WordWrap
                                text: qsTr("Bends the line of text into an arc: up for a smile, down for a frown.")
                                color: Theme.mutedForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                            }
                        }
                    }

                    // ---- Styles: looks and the whole layer stack ----
                    Loader {
                        width: parent.width
                        active: root.tab === "styles" && root.ready
                        visible: active
                        sourceComponent: Component {
                            TextInspector {
                                width: paneColumn.width
                                mode: "style"
                            }
                        }
                    }

                    // ---- Animation: In / Out / Loop ----
                    Loader {
                        width: parent.width
                        active: root.tab === "animation" && root.ready
                        visible: active
                        sourceComponent: Component {
                            TextInspector {
                                width: paneColumn.width
                                mode: "animate"
                            }
                        }
                    }
                }
            }
        }
    }
}
