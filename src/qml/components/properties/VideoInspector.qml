import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Shapes
import Drift

// Dafin: Filmora's Video tab for a video or image clip, opened when one is selected.
//
//   Basic   Transform (scale or width/height, position, rotation, flip and 90° turns),
//           Compositing (blend mode, opacity), and the Chroma Key switch: switched on it reads the
//           backdrop colour from the frame and opens Select Color, Tolerance, Pedestal, Spill
//           Suppression, Edge Feather, Edge Thickness and Alpha Channel. Reset puts it all back.
//   Mask    None, Rectangle, Ellipse, Lines, Heart, Star, Polygon, or an image or video as the
//           mask; then its rotation, size, position, blur and invert.
//
// Every value is keyframeable from its diamond, through the same rows the other tabs use. The
// chroma key is Drift's Chroma Key effect on the clip's effects stack ("fx.<i>.<param>"), the mask
// the mask adjustment pinned to the clip ("mask.<param>"); both are reached from the clip itself,
// so the clip stays selected.
Item {
    id: root

    // "basic" or "mask"; open sections — kept by PropertiesPanel across tab switches.
    property string page: "basic"
    property var sections: ({ transform: true, compositing: false, chroma: true })

    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property int trackIndex: EditorState.selectedTrack
    readonly property int clipIndex: EditorState.selectedClip
    readonly property int canvasW: {
        void EditorState.tracksRevision
        return Math.max(1, EditorState.projectFile.projectWidth())
    }
    readonly property int canvasH: {
        void EditorState.tracksRevision
        return Math.max(1, EditorState.projectFile.projectHeight())
    }

    function keyPoints(name) {
        return (root.clipData && root.clipData.keyframes && root.clipData.keyframes[name]
                && root.clipData.keyframes[name].points) || []
    }
    function toggleSection(name) {
        const next = Object.assign({}, root.sections)
        next[name] = !next[name]
        root.sections = next
    }

    // ---- Scale (as the Transform tab does it: one slider about the box centre) ----------------
    property var scaleDragStart: null
    property int liveRevision: 0
    readonly property real scaleAtPlayhead: {
        void clipDataRevision
        void liveRevision
        void EditorState.inspectorPlayheadSeconds
        if (!hasSelection)
            return 1
        return EditorState.keyframes.scaleOf(transformAt("width", canvasW), transformAt("height", canvasH),
                                             canvasW, canvasH)
    }
    function transformAt(key, def) {
        return EditorState.propertyValueAt(trackIndex, clipIndex, key, EditorState.inspectorPlayheadSeconds, def)
    }
    function keyedAtPlayhead(key) {
        const t = EditorState.inspectorPlayheadSeconds
        return keyPoints(key).some(p => Math.abs(p.seconds - t) <= 1 / 30)
    }
    function beginScaleDrag() {
        scaleDragStart = {
            "x": transformAt("x", 0),
            "y": transformAt("y", 0),
            "w": transformAt("width", canvasW),
            "h": transformAt("height", canvasH),
            "scale": scaleAtPlayhead,
            "rot": transformAt("rotation", 0) * Math.PI / 180,
            "pixelSize": 64
        }
    }
    function applyScale(scale) {
        const s = scaleDragStart
        if (!s || s.scale <= 0)
            return
        const r = EditorState.keyframes.scaledRect(s, scale, Qt.point(0.5, 0.5))
        EditorState.previewSetClipRect(trackIndex, clipIndex, r.x, r.y, r.w, r.h)
    }

    // ---- Chroma key -------------------------------------------------------------------------------
    readonly property var chroma: {
        void clipDataRevision
        return hasSelection ? EditorState.chromaKeyInfo(trackIndex, clipIndex) : ({})
    }
    readonly property bool chromaPresent: !!chroma.present
    readonly property bool chromaOn: chromaPresent && !!chroma.enabled
    readonly property int chromaIndex: chromaPresent ? chroma.effectIndex : -1
    readonly property var chromaEffect: {
        const list = (clipData && clipData.effects) || []
        return chromaIndex >= 0 && chromaIndex < list.length ? list[chromaIndex] : null
    }
    function chromaParam(key) {
        const params = (chromaEffect && chromaEffect.params) || []
        for (let i = 0; i < params.length; ++i) {
            if (params[i].key === key)
                return params[i]
        }
        return null
    }
    function chromaValue(param) {
        if (!param)
            return 0
        const keys = (param.keyframes && param.keyframes.points) || []
        const v = keys.length === 0 ? Number(param.value)
                                    : EditorState.propertyValueAt(trackIndex, clipIndex, param.prop,
                                                                  EditorState.inspectorPlayheadSeconds, param.value)
        return isNaN(v) ? 0 : v
    }
    // The key is a hue (see the Chroma Key shader); the swatch shows it fully saturated.
    function hueToHex(hue) {
        return Qt.hsva((((hue % 360) + 360) % 360) / 360, 1, 1, 1).toString()
    }
    function hexToHue(hex) {
        const c = Qt.lighter(hex, 1)
        return c.hsvHue < 0 ? NaN : c.hsvHue * 360
    }

    // ---- Mask -------------------------------------------------------------------------------------
    readonly property var mask: (hasSelection && clipData.mask) || ({})
    readonly property string maskShape: mask.shape || "none"
    function maskKeys(name) {
        const keys = root.mask.keyframes
        const entry = keys && keys[name]
        return (entry && entry.points) || []
    }
    function setMask(changes) {
        EditorState.setClipMask(trackIndex, clipIndex, Object.assign({}, root.mask, changes))
    }

    // Draw Mask: Drift's mask brush, aimed at this clip's own mask (MaskingController.brushOnHost),
    // so strokes on the preview paint it and the clip stays selected. A painted mask is a "media"
    // mask whose file the brush wrote, which is what tells it apart from Import.
    readonly property bool maskIsBrush: root.maskShape === "media" && !!root.mask.brush
    readonly property string maskTile: maskIsBrush ? "brush" : maskShape
    readonly property bool drawing: EditorState.preview.maskingTool === "brush" && EditorState.masking.brushOnHost
    function startDrawing() {
        EditorState.masking.brushErase = false
        EditorState.masking.brushOnHost = true
        EditorState.preview.maskingTool = "brush"
    }
    function stopDrawing() {
        if (!EditorState.masking.brushOnHost)
            return
        if (EditorState.preview.maskingTool === "brush")
            EditorState.preview.maskingTool = ""
        EditorState.masking.brushOnHost = false
    }
    // Fill or empty the clip's own mask; the brush's commit reads brushOnHost synchronously.
    function fillMask(clear) {
        const was = EditorState.masking.brushOnHost
        EditorState.masking.brushOnHost = true
        EditorState.masking.brushFill(root.trackIndex, root.clipIndex, clear)
        if (!was)
            EditorState.masking.brushOnHost = false
    }
    onPageChanged: if (page !== "mask") stopDrawing()
    onVisibleChanged: if (!visible) stopDrawing()
    Component.onDestruction: stopDrawing()
    Connections {
        target: EditorState.preview
        // Another tool took the preview (the Masking panel, crop…): Draw Mask is over.
        function onMaskingToolChanged() {
            if (EditorState.preview.maskingTool !== "brush" && EditorState.masking.brushOnHost)
                EditorState.masking.brushOnHost = false
        }
    }

    height: column.height
    implicitHeight: column.height

    function refreshFields() {}

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++ }
        function onSelectedClipDataChanged() { root.clipDataRevision++ }
        function onTracksChanged() { root.clipDataRevision++ }
    }

    // A labelled slider for the brush settings (as the Masking panel lays them out).
    component BrushSlider: Row {
        id: brushRow
        property string label: ""
        property real from: 0
        property real to: 100
        property real current: 0
        property string suffix: " %"
        signal moved(real value)
        width: parent ? parent.width : 0
        spacing: Theme.spacingSm
        Text {
            width: 70
            anchors.verticalCenter: parent.verticalCenter
            text: brushRow.label
            color: Theme.c.transformInspector.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            elide: Text.ElideRight
        }
        ThemedSlider {
            id: brushSlider
            width: brushRow.width - 70 - brushRow.spacing
            anchors.verticalCenter: parent.verticalCenter
            label: brushRow.label
            from: brushRow.from
            to: brushRow.to
            valueFormatter: function (v) { return Number(v).toFixed(brushRow.to <= 50 ? 1 : 0) + brushRow.suffix }
            Binding on value {
                when: !brushSlider.pressed
                value: brushRow.current
            }
            onMoved: brushRow.moved(value)
        }
    }

    // A section's header: an optional switch, the title, and the arrow that folds it.
    component SectionHeader: Item {
        id: header
        property string title: ""
        property bool open: true
        property bool hasSwitch: false
        property bool switchOn: false
        signal toggled()
        signal switched(bool on)

        width: parent ? parent.width : 0
        height: 30

        Rectangle {
            anchors.fill: parent
            radius: 4
            color: Theme.panelSecondaryBg
        }
        Row {
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            ThemedSwitch {
                visible: header.hasSwitch
                anchors.verticalCenter: parent.verticalCenter
                Binding on checked { value: header.switchOn }
                onToggled: header.switched(checked)
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: header.title
                color: Theme.panelForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                font.weight: Font.DemiBold
            }
            IconGlyph {
                anchors.verticalCenter: parent.verticalCenter
                glyph: Theme.icons.chevronDown
                iconSize: 12
                rotation: header.open ? 0 : -90
            }
        }
        MouseArea {
            // Leaves the switch its own clicks.
            anchors.fill: parent
            anchors.leftMargin: header.hasSwitch ? 52 : 0
            cursorShape: Qt.PointingHandCursor
            onClicked: header.toggled()
        }
    }

    component RowLabel: Text {
        color: Theme.c.transformInspector.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    Column {
        id: column
        width: root.width
        spacing: Theme.spacingLg

        // Basic | Mask
        Row {
            spacing: 6
            SearchAnchor {
                searchId: "video-basic"
                title: qsTr("Video")
                keywords: "size position scale chroma key green screen mask"
            }
            ThemedChip {
                text: qsTr("Basic")
                selected: root.page === "basic"
                onClicked: root.page = "basic"
            }
            ThemedChip {
                text: qsTr("Mask")
                selected: root.page === "mask"
                onClicked: root.page = "mask"
            }
        }

        // =================================== Basic ===================================
        Column {
            visible: root.page === "basic"
            width: parent.width
            spacing: Theme.spacingLg

            SectionHeader {
                title: qsTr("Transform")
                open: !!root.sections.transform
                onToggled: root.toggleSection("transform")
            }
            Column {
                visible: !!root.sections.transform
                width: parent.width
                spacing: Theme.spacingMd

                // Scale (linked) or Width/Height; the lock switches between them, as in Filmora.
                Row {
                    width: parent.width
                    spacing: 4
                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        buttonSize: 22
                        iconSize: 13
                        glyph: EditorState.sizeLinked ? Theme.icons.lock : Theme.icons.lockOpen
                        active: EditorState.sizeLinked
                        tooltip: EditorState.sizeLinked ? qsTr("Set width and height separately")
                                                        : qsTr("Scale width and height together")
                        onClicked: EditorState.sizeLinked = !EditorState.sizeLinked
                    }
                    RowLabel {
                        visible: EditorState.sizeLinked
                        width: 54
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Scale")
                    }
                    ThemedSlider {
                        id: scaleSlider
                        visible: EditorState.sizeLinked
                        lockWhilePlaying: true
                        label: qsTr("Scale")
                        width: parent.width - 22 - 54 - scaleReadout.width - scaleKey.width - parent.spacing * 4
                        anchors.verticalCenter: parent.verticalCenter
                        from: 0.05
                        to: 4
                        valueFormatter: function (v) { return Math.round(v * 100) + "%" }
                        Binding on value {
                            when: !scaleSlider.pressed
                            value: root.scaleAtPlayhead
                        }
                        onPressedChanged: {
                            if (pressed) {
                                root.beginScaleDrag()
                                EditorState.beginPreviewDrag(qsTr("Scale clip"))
                            } else {
                                EditorState.commitPreviewDrag()
                                root.liveRevision++
                            }
                        }
                        onMoved: root.applyScale(value)
                    }
                    Text {
                        id: scaleReadout
                        visible: EditorState.sizeLinked
                        width: 44
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Text.AlignRight
                        text: Math.round((scaleSlider.pressed ? scaleSlider.value : root.scaleAtPlayhead) * 100) + "%"
                        color: Theme.panelForeground
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.fontSizeSm
                    }
                    IconButton {
                        id: scaleKey
                        visible: EditorState.sizeLinked
                        anchors.verticalCenter: parent.verticalCenter
                        buttonSize: 24
                        iconSize: 15
                        readonly property bool keyed: root.keyedAtPlayhead("width") && root.keyedAtPlayhead("height")
                        glyph: keyed ? Theme.icons.diamondMinus : Theme.icons.diamondPlus
                        active: keyed
                        tooltip: keyed ? qsTr("Remove scale keyframe") : qsTr("Add scale keyframe")
                        onClicked: {
                            if (keyed)
                                EditorState.keyframes.removeKeysAt(root.trackIndex, root.clipIndex,
                                                                   ["width", "height"], EditorState.inspectorPlayheadSeconds)
                            else
                                EditorState.keyframes.addKeysAt(root.trackIndex, root.clipIndex,
                                                                ["width", "height"], EditorState.inspectorPlayheadSeconds)
                        }
                    }
                }
                PropertyKeyframeRow {
                    visible: !EditorState.sizeLinked
                    width: parent.width
                    propDef: ({ key: "width", label: qsTr("Width"), def: root.canvasW, decimals: 0 })
                    keyframeList: root.keyPoints("width")
                    useSlider: true
                    sliderFrom: 1
                    sliderTo: Math.max(root.canvasW * 2, 2)
                    unit: "px"
                }
                PropertyKeyframeRow {
                    visible: !EditorState.sizeLinked
                    width: parent.width
                    propDef: ({ key: "height", label: qsTr("Height"), def: root.canvasH, decimals: 0 })
                    keyframeList: root.keyPoints("height")
                    useSlider: true
                    sliderFrom: 1
                    sliderTo: Math.max(root.canvasH * 2, 2)
                    unit: "px"
                }

                PropertyKeyframeRow {
                    width: parent.width
                    propDef: ({ key: "x", label: qsTr("Position X"), def: 0.0, decimals: 0 })
                    keyframeList: root.keyPoints("x")
                    linkedPropKey: EditorState.positionLinked ? "y" : ""
                    useSlider: true
                    sliderFrom: -root.canvasW
                    sliderTo: root.canvasW * 2
                    unit: "px"
                }
                PropertyKeyframeRow {
                    width: parent.width
                    propDef: ({ key: "y", label: qsTr("Position Y"), def: 0.0, decimals: 0 })
                    keyframeList: root.keyPoints("y")
                    linkedPropKey: EditorState.positionLinked ? "x" : ""
                    useSlider: true
                    sliderFrom: -root.canvasH
                    sliderTo: root.canvasH * 2
                    unit: "px"
                }
                PropertyKeyframeRow {
                    width: parent.width
                    propDef: ({ key: "rotation", label: qsTr("Rotate"), def: 0.0, decimals: 1 })
                    keyframeList: root.keyPoints("rotation")
                    useSlider: true
                    sliderFrom: -180
                    sliderTo: 180
                    unit: "°"
                }

                // Flip horizontally, flip vertically, turn 90° left, turn 90° right.
                Row {
                    spacing: 6
                    RowLabel {
                        width: 54
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Flip")
                    }
                    IconButton {
                        buttonSize: 28
                        iconSize: 15
                        glyph: Theme.icons.foldHorizontal
                        active: !!root.clipData.flipH
                        tooltip: qsTr("Flip horizontally")
                        onClicked: EditorState.setClipFlip(root.trackIndex, root.clipIndex,
                                                           !root.clipData.flipH, !!root.clipData.flipV)
                    }
                    IconButton {
                        buttonSize: 28
                        iconSize: 15
                        rotation: 90
                        glyph: Theme.icons.foldHorizontal
                        active: !!root.clipData.flipV
                        tooltip: qsTr("Flip vertically")
                        onClicked: EditorState.setClipFlip(root.trackIndex, root.clipIndex,
                                                           !!root.clipData.flipH, !root.clipData.flipV)
                    }
                    IconButton {
                        buttonSize: 28
                        iconSize: 15
                        glyph: Theme.icons.reset
                        tooltip: qsTr("Turn 90° left")
                        onClicked: EditorState.setClipRotationSnap(root.trackIndex, root.clipIndex,
                                                                   Math.round(root.transformAt("rotation", 0)) - 90)
                    }
                    IconButton {
                        buttonSize: 28
                        iconSize: 15
                        glyph: Theme.icons.refresh
                        tooltip: qsTr("Turn 90° right")
                        onClicked: EditorState.setClipRotationSnap(root.trackIndex, root.clipIndex,
                                                                   Math.round(root.transformAt("rotation", 0)) + 90)
                    }
                }
            }

            SectionHeader {
                title: qsTr("Compositing")
                open: !!root.sections.compositing
                onToggled: root.toggleSection("compositing")
            }
            Column {
                visible: !!root.sections.compositing
                width: parent.width
                spacing: Theme.spacingMd
                BlendingInspector {
                    width: parent.width
                }
                PropertyKeyframeRow {
                    width: parent.width
                    propDef: ({ key: "opacity", label: qsTr("Opacity"), def: 1.0, decimals: 2 })
                    keyframeList: root.keyPoints("opacity")
                    useSlider: true
                    sliderFrom: 0
                    sliderTo: 1
                    percent: true
                }
            }

            SectionHeader {
                title: qsTr("Chroma Key")
                hasSwitch: true
                switchOn: root.chromaOn
                open: !!root.sections.chroma && root.chromaOn
                onToggled: root.toggleSection("chroma")
                onSwitched: (on) => {
                    if (on && !root.sections.chroma)
                        root.toggleSection("chroma")
                    EditorState.setChromaKeyOn(root.trackIndex, root.clipIndex, on)
                }
                SearchAnchor {
                    searchId: "video-chroma-key"
                    title: qsTr("Chroma Key")
                    keywords: "green screen blue screen key background remove"
                }
            }
            Column {
                visible: root.chromaOn && !!root.sections.chroma && !!root.chromaEffect
                width: parent.width
                spacing: Theme.spacingMd

                Row {
                    id: colorRow
                    width: parent.width
                    spacing: 8
                    readonly property var keyHue: root.chromaParam("u_keyHue")
                    RowLabel {
                        width: parent.width - 148
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Select Color")
                    }
                    ColorSwatchField {
                        anchors.verticalCenter: parent.verticalCenter
                        hex: root.hueToHex(root.chromaValue(colorRow.keyHue))
                        tooltip: qsTr("Pick the backdrop colour (the eyedropper picks it from the preview)")
                        // The key is turned off while picking, so the backdrop shows to be picked.
                        onEyedropperStarted: {
                            EditorState.beginPreviewDrag()
                            EditorState.previewSetEffectEnabled(root.trackIndex, root.clipIndex, root.chromaIndex, false)
                        }
                        onEyedropperEnded: EditorState.cancelPreviewDrag()
                        onEdited: (value) => {
                            const param = colorRow.keyHue
                            const deg = root.hexToHue(value)
                            // Grey has no hue; the field also re-emits its value on focus loss.
                            if (!param || isNaN(deg) || Math.abs(deg - root.chromaValue(param)) < 0.01)
                                return
                            EditorState.beginPreviewDrag(qsTr("Edit %1").arg(qsTr("Select Color")))
                            EditorState.previewSetClipKeyframe(root.trackIndex, root.clipIndex, param.prop,
                                                               EditorState.playheadSeconds, deg)
                            EditorState.commitPreviewDrag()
                        }
                    }
                }

                Repeater {
                    // Filmora's names for Drift's Chroma Key parameters.
                    model: [
                        { key: "u_tolerance", label: qsTr("Tolerance"), percent: true },
                        { key: "u_pedestal", label: qsTr("Pedestal"), percent: true },
                        { key: "u_spill", label: qsTr("Spill Suppression"), percent: true },
                        { key: "u_softness", label: qsTr("Edge Feather"), percent: true },
                        { key: "u_edgeThickness", label: qsTr("Edge Thickness"), percent: false }
                    ]
                    delegate: PropertyKeyframeRow {
                        required property var modelData
                        readonly property var param: root.chromaParam(modelData.key)
                        visible: !!param
                        width: parent.width
                        propDef: ({
                            key: (param && param.prop) || "",
                            label: modelData.label,
                            def: param ? Number(param.value) : 0,
                            decimals: modelData.percent ? 2 : 1
                        })
                        keyframeList: (param && param.keyframes && param.keyframes.points) || []
                        useSlider: true
                        editableWhilePlaying: true
                        sliderFrom: param ? param.min : 0
                        sliderTo: param ? param.max : 1
                        percent: modelData.percent
                        unit: modelData.percent ? "" : "px"
                    }
                }

                Row {
                    id: alphaRow
                    width: parent.width
                    spacing: 8
                    readonly property var alphaParam: root.chromaParam("u_alphaView")
                    visible: !!alphaParam
                    RowLabel {
                        width: parent.width - alphaSwitch.width - 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Alpha Channel")
                    }
                    ThemedSwitch {
                        id: alphaSwitch
                        anchors.verticalCenter: parent.verticalCenter
                        tooltip: qsTr("Show the key's matte: white is kept, black is taken out")
                        Binding on checked { value: root.chromaValue(alphaRow.alphaParam) > 0.5 }
                        onToggled: EditorState.setEffectParam(root.trackIndex, root.clipIndex, root.chromaIndex,
                                                              "u_alphaView", checked ? 1 : 0)
                    }
                }

                ThemedButton {
                    text: qsTr("Reset chroma key")
                    variant: "secondary"
                    tooltip: qsTr("Back to the default settings, keeping the selected colour")
                    onClicked: EditorState.resetChromaKey(root.trackIndex, root.clipIndex)
                }
            }

            Item {
                width: parent.width
                height: resetButton.height
                ThemedButton {
                    id: resetButton
                    anchors.right: parent.right
                    text: qsTr("Reset")
                    tooltip: qsTr("Puts size, position, rotation, flips, opacity and the chroma key settings back")
                    onClicked: EditorState.resetVideoBasics(root.trackIndex, root.clipIndex)
                }
            }
        }

        // =================================== Mask ====================================
        Column {
            visible: root.page === "mask"
            width: parent.width
            spacing: Theme.spacingLg

            Grid {
                id: shapeGrid
                width: parent.width
                columns: 4
                spacing: 8
                readonly property real tile: Math.floor((width - spacing * (columns - 1)) / columns)
                SearchAnchor {
                    searchId: "video-mask"
                    title: qsTr("Mask")
                    keywords: "shape cut out rectangle circle heart star"
                }

                Repeater {
                    model: [
                        { id: "none", label: qsTr("None") },
                        { id: "rectangle", label: qsTr("Rectangle") },
                        { id: "ellipse", label: qsTr("Ellipse") },
                        { id: "bars", label: qsTr("Lines") },
                        { id: "heart", label: qsTr("Heart") },
                        { id: "star", label: qsTr("Star") },
                        { id: "freeform", label: qsTr("Polygon") },
                        { id: "brush", label: qsTr("Draw Mask — paint the mask on the preview") },
                        { id: "media", label: qsTr("Import") }
                    ]
                    delegate: Rectangle {
                        id: shapeTile
                        required property var modelData
                        readonly property bool selected: modelData.id === "brush"
                                                         ? root.drawing || root.maskIsBrush
                                                         : root.maskTile === modelData.id && !root.drawing
                        width: shapeGrid.tile
                        height: shapeGrid.tile
                        radius: 6
                        color: tileMouse.containsMouse ? Theme.panelAccent : Theme.panelSecondaryBg
                        border.width: selected ? 2 : 1
                        border.color: selected ? Theme.primary : Theme.panelBorder

                        readonly property bool iconTile: modelData.id === "none" || modelData.id === "media"
                                                         || modelData.id === "freeform" || modelData.id === "brush"
                        IconGlyph {
                            visible: shapeTile.iconTile
                            anchors.centerIn: parent
                            iconSize: Math.round(shapeTile.width * 0.38)
                            glyph: shapeTile.modelData.id === "none" ? Theme.icons.x
                                 : shapeTile.modelData.id === "media" ? Theme.icons.upload
                                 : shapeTile.modelData.id === "brush" ? Theme.icons.brush
                                 : Theme.icons.pentagon
                            iconColor: shapeTile.selected ? Theme.primary : Theme.panelForeground
                        }
                        // The shape the compositor cuts, from the same path (as the Masks browser).
                        Item {
                            visible: !shapeTile.iconTile
                            width: 100
                            height: 100
                            anchors.centerIn: parent
                            scale: shapeTile.width * 0.62 / 100
                            Shape {
                                anchors.fill: parent
                                antialiasing: true
                                ShapePath {
                                    strokeColor: shapeTile.selected ? Theme.primary : Theme.panelForeground
                                    strokeWidth: 100 / (shapeTile.width * 0.62) * 1.5
                                    fillColor: "transparent"
                                    PathSvg {
                                        path: shapeTile.iconTile
                                              ? "" : EditorState.maskShapeSvgPath(shapeTile.modelData.id)
                                    }
                                }
                            }
                        }
                        ThemedToolTip {
                            visible: tileMouse.containsMouse
                            text: shapeTile.modelData.label
                        }
                        MouseArea {
                            id: tileMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                const id = shapeTile.modelData.id
                                if (id === "brush") {
                                    if (root.drawing)
                                        root.stopDrawing()
                                    else
                                        root.startDrawing()
                                    return
                                }
                                root.stopDrawing()
                                if (id === "media") {
                                    const url = FileDialogs.openFile(
                                        qsTr("Choose a mask image or video"),
                                        [qsTr("Media files (*.png *.jpg *.jpeg *.webp *.heic *.heif *.avif *.tif *.tiff *.bmp *.gif *.mp4 *.mov *.mkv *.webm)"),
                                         qsTr("All files (*)")])
                                    if (url && url.toString() !== "")
                                        EditorState.addMediaMaskToClip(root.trackIndex, root.clipIndex, url)
                                    return
                                }
                                if (id === root.maskShape)
                                    return
                                // A new mask starts from the shape's defaults; changing shape keeps
                                // where the mask sits.
                                root.setMask(root.maskShape === "none" || root.maskShape === "media"
                                             ? { shape: id, invert: false } : { shape: id })
                            }
                        }
                    }
                }
            }

            // ---- Draw Mask: the brush ----
            Column {
                visible: root.drawing || root.maskIsBrush
                width: parent.width
                spacing: Theme.spacingMd

                SectionHeader {
                    title: qsTr("Draw Mask")
                    hasSwitch: true
                    switchOn: root.drawing
                    open: true
                    onSwitched: (on) => on ? root.startDrawing() : root.stopDrawing()
                }

                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: root.drawing
                          ? qsTr("Paint on the preview to show the clip there. Right-drag erases; Shift-drag changes the brush size.")
                          : qsTr("Turn Draw Mask on to keep painting.")
                    color: Theme.c.transformInspector.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                BrushSlider {
                    label: qsTr("Size")
                    from: 0.2
                    to: 50
                    current: EditorState.masking.brushSize * 100
                    onMoved: (v) => EditorState.masking.brushSize = v / 100
                }
                BrushSlider {
                    label: qsTr("Hardness")
                    to: 100
                    current: EditorState.masking.brushHardness * 100
                    onMoved: (v) => EditorState.masking.brushHardness = v / 100
                }
                BrushSlider {
                    label: qsTr("Opacity")
                    from: 1
                    to: 100
                    current: EditorState.masking.brushOpacity * 100
                    onMoved: (v) => EditorState.masking.brushOpacity = v / 100
                }

                Row {
                    spacing: Theme.spacingSm
                    ThemedToggleButton {
                        text: qsTr("Paint")
                        tooltip: qsTr("Left-drag paints the mask")
                        checked: !EditorState.masking.brushErase
                        onClicked: EditorState.masking.brushErase = false
                    }
                    ThemedToggleButton {
                        text: qsTr("Erase")
                        tooltip: qsTr("Left-drag erases the mask")
                        checked: EditorState.masking.brushErase
                        onClicked: EditorState.masking.brushErase = true
                    }
                    ThemedButton {
                        text: qsTr("Fill")
                        variant: "secondary"
                        tooltip: qsTr("Cover the whole clip with the mask, then erase what should go")
                        onClicked: root.fillMask(false)
                    }
                    ThemedButton {
                        text: qsTr("Clear")
                        variant: "secondary"
                        enabled: root.maskIsBrush
                        tooltip: qsTr("Empty the mask")
                        onClicked: root.fillMask(true)
                    }
                }
            }

            Column {
                visible: root.maskShape !== "none"
                width: parent.width
                spacing: Theme.spacingMd

                SectionHeader {
                    title: qsTr("Mask")
                    open: true
                }

                Repeater {
                    // What the rasterizer reads for each shape (MasksInspector): Lines derive from
                    // the height alone, a Polygon's points are the shape, a file mask is full-frame.
                    model: [
                        { key: "mask.rotation", label: qsTr("Rotate"), def: 0, decimals: 1, min: -180, max: 180,
                          unit: "°", shapes: ["rectangle", "ellipse", "star", "heart"] },
                        { key: "mask.w", label: qsTr("Width"), def: 0.6, decimals: 2, min: 0.05, max: 1,
                          percent: true, shapes: ["rectangle", "ellipse", "star", "heart"] },
                        { key: "mask.h", label: qsTr("Height"), def: 0.6, decimals: 2, min: 0.05, max: 1,
                          percent: true, shapes: ["rectangle", "ellipse", "star", "heart", "bars"] },
                        { key: "mask.x", label: qsTr("Position X"), def: 0.5, decimals: 2, min: 0, max: 1,
                          percent: true, shapes: ["rectangle", "ellipse", "star", "heart"] },
                        { key: "mask.y", label: qsTr("Position Y"), def: 0.5, decimals: 2, min: 0, max: 1,
                          percent: true, shapes: ["rectangle", "ellipse", "star", "heart", "bars"] },
                        { key: "mask.feather", label: qsTr("Blur Strength"), def: 0, decimals: 0, min: 0, max: 64,
                          unit: "px", shapes: ["rectangle", "ellipse", "star", "heart", "bars", "freeform"] }
                    ]
                    delegate: PropertyKeyframeRow {
                        required property var modelData
                        visible: modelData.shapes.indexOf(root.maskShape) >= 0
                        width: parent.width
                        propDef: ({
                            key: modelData.key,
                            label: modelData.label,
                            // The mask's own value when it is not animated.
                            def: root.mask[modelData.key.substring(5)] !== undefined
                                 ? Number(root.mask[modelData.key.substring(5)]) : modelData.def,
                            decimals: modelData.decimals
                        })
                        keyframeList: root.maskKeys(modelData.key.substring(5))
                        useSlider: true
                        sliderFrom: modelData.min
                        sliderTo: modelData.max
                        percent: !!modelData.percent
                        unit: modelData.unit || ""
                    }
                }

                Row {
                    width: parent.width
                    spacing: 8
                    RowLabel {
                        width: parent.width - invertSwitch.width - 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Invert Mask")
                    }
                    ThemedSwitch {
                        id: invertSwitch
                        anchors.verticalCenter: parent.verticalCenter
                        Binding on checked { value: !!root.mask.invert }
                        onToggled: root.setMask({ invert: checked })
                    }
                }

                Text {
                    visible: root.maskShape === "freeform"
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: qsTr("Select the mask on the timeline to move the polygon's points on the preview.")
                    color: Theme.c.transformInspector.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                Row {
                    spacing: 8
                    ThemedButton {
                        visible: root.maskShape !== "media"
                        text: qsTr("Reset")
                        tooltip: qsTr("Back to the shape's default size and place")
                        onClicked: root.setMask({ x: 0.5, y: 0.5, w: 0.6, h: 0.6, rotation: 0, feather: 0,
                                                  invert: false })
                    }
                    ThemedButton {
                        text: root.maskShape === "media" ? qsTr("Remove cutout layer") : qsTr("Remove mask")
                        variant: "destructive"
                        glyph: Theme.icons.trash
                        onClicked: root.setMask({ shape: "none" })
                    }
                }
            }
        }
    }
}
