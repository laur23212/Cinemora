import QtQuick
import QtQuick.Controls.Basic
import Drift
// The Speed tab. Dafin: laid out like Filmora's — Uniform Speed (a 0.01×–100× speed or a length,
// Reverse, Freeze frame) and Speed Ramping (None, Customize and six presets, and the ramp graph to
// shape it), both applied to the clip itself.
Item {
    id: root

    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property string clipKind: hasSelection ? (clipData.kind || "") : ""
    // Composites (and audio separated from one) read a nested timeline, which the Custom speed
    // preview and the reverse render cannot decode.
    readonly property bool nestedSource: hasSelection && !!clipData.sequenceId

    readonly property bool hasSpeedCurve: hasSelection && !!clipData.hasSpeedCurve
    readonly property real speed: hasSelection ? (clipData.speed || 1) : 1
    // A freeze frame is a held frame inside a clip (drift::isFreezeFrameClip): only its length is
    // its own to change.
    readonly property bool isFreeze: hasSelection && !hasSpeedCurve && speed > 0 && speed < 0.001
    readonly property bool rampable: (clipKind === "video" || clipKind === "audio") && !nestedSource && !isFreeze

    // "uniform" or "ramping"; right-click › Speed › Uniform Speed / Speed Ramping ask for one.
    property string page: "uniform"
    readonly property string shownPage: rampable ? page : "uniform"

    readonly property real minSpeed: EditorState.minUniformSpeed()
    readonly property real maxSpeed: EditorState.maxUniformSpeed()
    readonly property var rampPresets: EditorState.speedRampPresets()
    readonly property string currentPreset: {
        void clipDataRevision
        return hasSelection && hasSpeedCurve
               ? EditorState.clipSpeedRampPreset(EditorState.selectedTrack, EditorState.selectedClip)
               : "none"
    }
    readonly property var curvePoints: {
        void clipDataRevision
        return hasSelection ? EditorState.clipSpeedCurvePoints(EditorState.selectedTrack, EditorState.selectedClip)
                            : []
    }
    // While a ramp point is dragged the graph draws these; the project only takes the edit on release.
    property bool rampDragging: false
    property var livePoints: []
    // How long Add freeze frame holds (Filmora's default is five seconds).
    property real freezeSeconds: 5.0

    height: speedColumn.height
    implicitHeight: speedColumn.height

    function refreshFields() {}

    function takeSpeedPageRequest() {
        if (ShellBridge.speedPageRequest === "")
            return
        root.page = ShellBridge.speedPageRequest
        ShellBridge.speedPageRequest = ""
    }

    // Speed slider on a log scale, 0.01× at the left, 1× in the middle, 100× at the right.
    function sliderForSpeed(s) {
        const lo = Math.log(minSpeed) / Math.LN10
        const hi = Math.log(maxSpeed) / Math.LN10
        return (Math.log(Math.max(minSpeed, Math.min(maxSpeed, s))) / Math.LN10 - lo) / (hi - lo)
    }
    function speedForSlider(v) {
        const lo = Math.log(minSpeed) / Math.LN10
        const hi = Math.log(maxSpeed) / Math.LN10
        const s = Math.pow(10, lo + v * (hi - lo))
        // Snap near 1× so the middle of the track lands on normal speed.
        if (Math.abs(s - 1) < 0.04)
            return 1
        return s < 0.1 ? Number(s.toFixed(3)) : Number(s.toFixed(2))
    }
    function formatSpeed(s) {
        return (s < 0.1 ? s.toFixed(3) : s.toFixed(2)) + "×"
    }

    function applyPreset(id) {
        const t = EditorState.selectedTrack
        const c = EditorState.selectedClip
        if (id === "custom") {
            if (root.hasSpeedCurve)
                return
            // Start from a flat ramp at the clip's speed, with a point in the middle to shape.
            const s = Math.max(0.1, Math.min(8, root.speed))
            rampGraph.selectedIndex = 1
            EditorState.setClipSpeedCurvePoints(t, c, rampGraph.smoothed([
                { pos: 0, speed: s, corner: false },
                { pos: 0.5, speed: s, corner: false },
                { pos: 1, speed: s, corner: false }]))
            return
        }
        EditorState.applySpeedRampPreset(t, c, id)
    }

    Connections {
        target: EditorState
        function onSelectionChanged() {
            root.clipDataRevision++
            // A clip with a ramp opens on its ramp.
            if (root.rampable && !!EditorState.selectedClipData.hasSpeedCurve)
                root.page = "ramping"
        }
        function onSelectedClipDataChanged() { root.clipDataRevision++ }
        function onTracksChanged() { root.clipDataRevision++ }
    }
    Connections {
        target: ShellBridge
        function onSpeedPageRequestChanged() { root.takeSpeedPageRequest() }
    }
    Component.onCompleted: {
        root.takeSpeedPageRequest()
        if (root.rampable && root.hasSpeedCurve && ShellBridge.speedPageRequest === "")
            root.page = "ramping"
    }

    Column {
        id: speedColumn
        width: root.width
        spacing: Theme.spacingXl

        // Uniform Speed | Speed Ramping
        Row {
            visible: root.rampable
            spacing: 6
            SearchAnchor {
                searchId: "speed-ramping"
                title: qsTr("Speed ramping")
                keywords: "ramp curve montage hero bullet time jumper flash"
            }
            ThemedChip {
                text: qsTr("Uniform Speed")
                selected: root.shownPage === "uniform"
                onClicked: root.page = "uniform"
            }
            ThemedChip {
                text: qsTr("Speed Ramping")
                selected: root.shownPage === "ramping"
                onClicked: root.page = "ramping"
            }
        }

        // ---- Freeze frame (a held frame selected) -------------------------------------------
        Column {
            visible: root.isFreeze
            width: parent.width
            spacing: Theme.spacingMd

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Freeze frame: this part holds one frame of the clip.")
                color: Theme.c.speedFadeInspector.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
            Row {
                spacing: 8
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Duration")
                    color: Theme.c.speedFadeInspector.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
                ThemedNumberField {
                    id: freezeLengthField
                    width: 90
                    decimals: 2
                    step: 0.1
                    from: 0.04
                    to: 3600
                    unit: "s"
                    Binding on value {
                        when: !freezeLengthField.activeFocus
                        value: root.hasSelection ? (root.clipData.duration || 0) : 0
                    }
                    onEdited: (v) => EditorState.setClipDurationSeconds(EditorState.selectedTrack,
                                                                        EditorState.selectedClip, v)
                }
            }
        }

        // ---- Uniform Speed ------------------------------------------------------------------
        Column {
            id: uniformPage
            visible: !root.isFreeze && root.shownPage === "uniform"
            width: parent.width
            spacing: Theme.spacingXl

            // A ramp supersedes the uniform speed outright, so it is said here and the controls
            // below wait until it is taken off.
            Row {
                visible: root.hasSpeedCurve
                width: parent.width
                spacing: Theme.spacingLg
                ThemedLabel {
                    width: parent.width - removeRampButton.width - Theme.spacingLg
                    anchors.verticalCenter: parent.verticalCenter
                    wrapMode: Text.WordWrap
                    text: qsTr("A speed ramp sets this clip's speed and length.")
                }
                ThemedButton {
                    id: removeRampButton
                    text: qsTr("Remove ramp")
                    onClicked: EditorState.applySpeedRampPreset(EditorState.selectedTrack,
                                                                EditorState.selectedClip, "none")
                }
            }

            Item {
                width: parent.width
                height: speedField.height
                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Speed")
                    SearchAnchor {
                        searchId: "speed-playback"
                        title: qsTr("Playback speed")
                        keywords: "fast slow motion uniform"
                    }
                    color: Theme.c.speedFadeInspector.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
                ThemedNumberField {
                    id: speedField
                    anchors.right: parent.right
                    width: 90
                    enabled: !root.hasSpeedCurve
                    decimals: 2
                    step: 0.05
                    from: root.minSpeed
                    to: root.maxSpeed
                    unit: "×"
                    Binding on value {
                        when: !speedField.activeFocus
                        value: root.speed
                    }
                    onEdited: (v) => EditorState.setClipSpeed(EditorState.selectedTrack,
                                                              EditorState.selectedClip, v)
                }
            }

            Column {
                width: parent.width
                spacing: 2
                ThemedSlider {
                    id: speedSlider
                    label: qsTr("Speed")
                    enabled: !root.hasSpeedCurve && !EditorState.playing
                    width: parent.width
                    from: 0
                    to: 1
                    valueFormatter: function (v) { return root.formatSpeed(root.speedForSlider(v)) }
                    Binding on value {
                        when: !speedSlider.pressed
                        value: root.sliderForSpeed(root.speed)
                    }
                    // Previewed while dragged; on release the preview is dropped and the speed set
                    // once, which also moves along whatever a slower clip would run into.
                    onMoved: EditorState.previewSetClipSpeed(EditorState.selectedTrack, EditorState.selectedClip,
                                                             root.speedForSlider(value))
                    onPressedChanged: {
                        if (pressed) {
                            EditorState.beginPreviewDrag(qsTr("Speed changed"))
                        } else {
                            const s = root.speedForSlider(value)
                            EditorState.cancelPreviewDrag()
                            EditorState.setClipSpeed(EditorState.selectedTrack, EditorState.selectedClip, s)
                        }
                    }
                }
                Item {
                    width: parent.width
                    height: slowLabel.height
                    Text {
                        id: slowLabel
                        text: qsTr("Slow")
                        color: Theme.c.speedFadeInspector.foregroundMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "1×"
                        color: Theme.c.speedFadeInspector.foregroundMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }
                    Text {
                        anchors.right: parent.right
                        text: qsTr("Fast")
                        color: Theme.c.speedFadeInspector.foregroundMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }
                }
            }

            Flow {
                width: parent.width
                spacing: 6
                Repeater {
                    model: [
                        { label: "0.25×", value: 0.25 },
                        { label: "0.5×", value: 0.5 },
                        { label: "1×", value: 1.0 },
                        { label: "2×", value: 2.0 },
                        { label: "4×", value: 4.0 },
                        { label: "8×", value: 8.0 }
                    ]
                    delegate: ThemedChip {
                        required property var modelData
                        text: modelData.label
                        enabled: !root.hasSpeedCurve
                        selected: !root.hasSpeedCurve && Math.abs(root.speed - modelData.value) < 0.01
                        onClicked: EditorState.setClipSpeed(EditorState.selectedTrack, EditorState.selectedClip,
                                                            modelData.value)
                    }
                }
            }

            // Filmora's Duration: the length the clip should play for, its speed following.
            Item {
                width: parent.width
                height: durationField.height
                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Duration")
                    SearchAnchor {
                        searchId: "speed-duration"
                        title: qsTr("Duration")
                        keywords: "length speed fit"
                    }
                    color: Theme.c.speedFadeInspector.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
                ThemedNumberField {
                    id: durationField
                    anchors.right: parent.right
                    width: 90
                    enabled: !root.hasSpeedCurve
                    decimals: 2
                    step: 0.1
                    from: 0.04
                    to: 360000
                    unit: "s"
                    Binding on value {
                        when: !durationField.activeFocus
                        value: root.hasSelection ? (root.clipData.duration || 0) : 0
                    }
                    onEdited: (v) => EditorState.setClipDurationSeconds(EditorState.selectedTrack,
                                                                        EditorState.selectedClip, v)
                }
            }

            Text {
                visible: !root.hasSpeedCurve && (root.speed < 0.1 - 1e-6 || root.speed > 8 + 1e-6)
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("No sound at this speed: sound plays from 0.1× to 8×.")
                color: Theme.c.speedFadeInspector.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            SearchAnchor {
                searchId: "speed-reverse"
                title: qsTr("Reverse")
                keywords: "backwards rewind"
                target: speedReverseChip
            }
            ThemedChip {
                id: speedReverseChip
                visible: !root.nestedSource
                text: qsTr("Reverse")
                selected: {
                    void root.clipDataRevision
                    return !!root.clipData.reverse
                }
                // Turning reverse off is free. Turning it on may need a rendered copy first, which
                // requestClipReverse decides and confirms through ReverseProgressDialog.
                onClicked: {
                    if (root.clipData.reverse)
                        EditorState.setClipReverse(EditorState.selectedTrack,
                                                   EditorState.selectedClip, false)
                    else
                        EditorState.requestClipReverse(EditorState.selectedTrack,
                                                       EditorState.selectedClip)
                }
            }

            // Shown when a reversed video clip has no rendered copy covering it — after a cancelled
            // render, or after a trim pushed the clip past the range that was rendered.
            Row {
                width: parent.width
                spacing: Theme.spacingLg
                visible: {
                    void root.clipDataRevision
                    void EditorState.reverseRendering
                    return root.clipKind === "video" && !!root.clipData.reverse
                           && !EditorState.clipHasReverseProxy(EditorState.selectedTrack,
                                                               EditorState.selectedClip)
                }

                ThemedLabel {
                    width: parent.width - renderReversedButton.width - Theme.spacingLg
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Not rendered — playback may stutter")
                }

                ThemedButton {
                    id: renderReversedButton
                    text: qsTr("Render")
                    onClicked: EditorState.requestClipReverse(EditorState.selectedTrack,
                                                              EditorState.selectedClip)
                }
            }

            // Freeze frame: hold the frame at the playhead inside the clip.
            Column {
                visible: root.clipKind === "video"
                width: parent.width
                spacing: Theme.spacingMd
                Text {
                    text: qsTr("Freeze frame")
                    SearchAnchor {
                        searchId: "speed-freeze"
                        title: qsTr("Freeze frame")
                        keywords: "hold still pause frame"
                    }
                    color: Theme.c.speedFadeInspector.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
                Row {
                    spacing: 8
                    ThemedNumberField {
                        id: freezeSecondsField
                        width: 80
                        anchors.verticalCenter: parent.verticalCenter
                        decimals: 1
                        step: 0.5
                        from: 0.1
                        to: 600
                        unit: "s"
                        Binding on value {
                            when: !freezeSecondsField.activeFocus
                            value: root.freezeSeconds
                        }
                        onEdited: (v) => root.freezeSeconds = v
                    }
                    ThemedButton {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Add at playhead")
                        glyph: Theme.icons.snowflake
                        tooltip: qsTr("Holds the frame at the playhead for this long; the rest of the clip moves later")
                        enabled: {
                            void root.clipDataRevision
                            const start = root.clipData.start || 0
                            return EditorState.playheadSeconds >= start
                                   && EditorState.playheadSeconds < start + (root.clipData.duration || 0)
                        }
                        onClicked: EditorState.insertFreezeFrame(EditorState.selectedTrack, EditorState.selectedClip,
                                                                 root.freezeSeconds)
                    }
                }
            }
        }

        // ---- Speed Ramping ------------------------------------------------------------------
        Column {
            visible: !root.isFreeze && root.shownPage === "ramping"
            width: parent.width
            spacing: Theme.spacingXl

            SpeedRampPresetGrid {
                width: parent.width
                presets: root.rampPresets
                current: root.hasSpeedCurve ? root.currentPreset : "none"
                onPicked: (id) => root.applyPreset(id)
            }

            Column {
                visible: root.hasSpeedCurve || root.rampDragging
                width: parent.width
                spacing: Theme.spacingMd

                Row {
                    width: parent.width
                    spacing: 6
                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: Theme.icons.plus
                        buttonSize: 24
                        iconSize: 14
                        tooltip: qsTr("Add a point at the playhead (or double-click the graph)")
                        onClicked: {
                            const list = rampGraph.copyPoints()
                            if (list.length < 2)
                                return
                            const start = root.clipData.start || 0
                            const length = Math.max(0.001, root.clipData.duration || 0)
                            const fraction = (EditorState.playheadSeconds - start) / length
                            let pos = fraction > 0 && fraction < 1 ? rampGraph.posForTimelineFraction(fraction) : -1
                            // Off the clip: halve the widest stretch between two points.
                            if (pos < 0) {
                                let best = 0
                                for (let i = 1; i < list.length; ++i) {
                                    if (list[i].pos - list[i - 1].pos > list[best + 1].pos - list[best].pos)
                                        best = i - 1
                                }
                                pos = (list[best].pos + list[best + 1].pos) / 2
                            }
                            let at = list.length - 1
                            for (let i = 1; i < list.length; ++i) {
                                if (list[i].pos > pos) {
                                    at = i
                                    break
                                }
                            }
                            if (pos - list[at - 1].pos < 0.02 || list[at].pos - pos < 0.02)
                                return
                            list.splice(at, 0, { pos: pos, speed: rampGraph.speedAt(pos), corner: false })
                            rampGraph.selectedIndex = at
                            EditorState.setClipSpeedCurvePoints(EditorState.selectedTrack, EditorState.selectedClip,
                                                                rampGraph.smoothed(list))
                        }
                    }
                    // Filmora's ❄ beside the +: holds the frame at the playhead inside the clip. The
                    // ramp stays on both halves around the hold.
                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: Theme.icons.snowflake
                        buttonSize: 24
                        iconSize: 14
                        enabled: {
                            void root.clipDataRevision
                            const start = root.clipData.start || 0
                            return root.clipData.kind === "video"
                                   && EditorState.playheadSeconds >= start
                                   && EditorState.playheadSeconds < start + (root.clipData.duration || 0)
                        }
                        tooltip: qsTr("Freeze frame — holds the frame at the playhead for %1 s; the rest of the clip moves later")
                                     .arg(root.freezeSeconds)
                        onClicked: EditorState.insertFreezeFrame(EditorState.selectedTrack, EditorState.selectedClip,
                                                                 root.freezeSeconds)
                    }
                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: Theme.icons.trash
                        buttonSize: 24
                        iconSize: 14
                        enabled: rampGraph.selectedIndex > 0 && rampGraph.selectedIndex < rampGraph.points.length - 1
                        tooltip: qsTr("Remove the selected point (or right-click it)")
                        onClicked: {
                            const list = rampGraph.copyPoints()
                            list.splice(rampGraph.selectedIndex, 1)
                            rampGraph.selectedIndex = -1
                            EditorState.setClipSpeedCurvePoints(EditorState.selectedTrack, EditorState.selectedClip,
                                                                rampGraph.smoothed(list))
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Duration %1 s").arg(((root.clipData.duration || 0)).toFixed(2))
                        color: Theme.c.speedFadeInspector.foregroundMuted
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }
                }

                Rectangle {
                    width: parent.width
                    height: 150
                    radius: 6
                    color: Theme.panelSecondaryBg
                    border.width: 1
                    border.color: Theme.panelBorder

                    SpeedRampGraph {
                        id: rampGraph
                        anchors.fill: parent
                        anchors.margins: 4
                        interactive: true
                        points: root.rampDragging ? root.livePoints : root.curvePoints
                        playheadFraction: {
                            const start = root.clipData.start || 0
                            const length = root.clipData.duration || 0
                            return length > 0 ? (EditorState.playheadSeconds - start) / length : -1
                        }
                        onPointsEdited: (points, committed) => {
                            const t = EditorState.selectedTrack
                            const c = EditorState.selectedClip
                            if (!committed) {
                                if (!root.rampDragging) {
                                    root.rampDragging = true
                                    EditorState.beginPreviewDrag(qsTr("Speed ramp changed"))
                                }
                                root.livePoints = points
                                EditorState.previewSetClipSpeedCurvePoints(t, c, points)
                                return
                            }
                            // The preview goes, and the ramp lands once as the undo step.
                            if (root.rampDragging) {
                                EditorState.cancelPreviewDrag()
                                root.rampDragging = false
                            }
                            EditorState.setClipSpeedCurvePoints(t, c, points)
                        }
                    }
                }

                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: qsTr("Drag a point up to speed up, down to slow down. Double-click the graph to add one, right-click a point to remove it.")
                    color: Theme.c.speedFadeInspector.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }

            ThemedButton {
                visible: !root.nestedSource
                text: qsTr("Advanced curve editor…")
                variant: "secondary"
                tooltip: qsTr("The full curve editor with its own preview; it applies the curve to a copy on a new track")
                SearchAnchor {
                    searchId: "speed-custom"
                    title: qsTr("Custom speed…")
                    keywords: "ramp curve"
                }
                onClicked: ShellBridge.main.openSpeedCurve(EditorState.selectedTrack, EditorState.selectedClip)
            }
        }
    }
}
