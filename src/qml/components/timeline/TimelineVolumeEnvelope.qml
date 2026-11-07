import QtQuick
import QtQuick.Shapes
import Drift

// Cool Edit Pro-style volume line drawn across a clip that carries sound. The line is the clip's
// "volume" keyframes — the same ones the Properties panel edits and that playback, the mixer and
// export already honour — so this is only a faster way to shape them:
//
//   click the line          add a dot there (and keep dragging to place it)
//   drag a dot              move it: sideways = when, up/down = how loud
//   Shift + drag            change only the level
//   double-click a dot      remove it (right-click does the same)
//
// The middle of the clip is 100 %; the top is +6 dB (200 %), the bottom is silence. With no dots
// the line is flat at the clip's volume. The first click on a flat line also pins its two ends,
// so a single dot makes a bump rather than lifting the whole clip; all of it is one undo step.
//
// Presses away from the line fall through to the clip below (select / move), so the clip still
// handles exactly as before. Fade dots and trim handles stay on top of this.
Item {
    id: root

    required property var owner                // TimelineClipItem

    readonly property var clip: owner.clipData
    readonly property bool carriesAudio: clip.kind === "audio"
                                         || (clip.kind === "video" && !!clip.hasEmbeddedAudio)
    readonly property real maxGain: 2.0          // +6 dB, the engine's ceiling
    readonly property real dotRadius: 4
    readonly property real hitSlop: 6
    readonly property real minGapSeconds: 0.02   // two dots never share a time

    visible: carriesAudio && !owner.touchMode && height >= 18
    anchors.fill: parent

    // ── geometry ─────────────────────────────────────────────────────────────────────────────
    readonly property real pxPerSecond: owner.panel.pxPerSecond
    readonly property real clipStart: owner.effectiveStart
    readonly property real clipDuration: Math.max(0.001, owner.effectiveDuration)
    // Leave room for the dot at the top and bottom so +6 dB and silence stay grabbable.
    readonly property real topPad: dotRadius + 2
    readonly property real usable: Math.max(1, height - 2 * topPad)

    function yForGain(v) { return topPad + (1 - Math.max(0, Math.min(maxGain, v)) / maxGain) * usable }
    function gainForY(y) { return Math.max(0, Math.min(maxGain, (1 - (y - topPad) / usable) * maxGain)) }
    function xForTime(t) { return t * width / clipDuration }
    function timeForX(x) { return Math.max(0, Math.min(clipDuration, x / width * clipDuration)) }
    function dbText(v) {
        if (v <= 0.0001)
            return "-∞ dB"
        const db = 20 * Math.log(v) / Math.LN10
        return (db > 0 ? "+" : "") + db.toFixed(1) + " dB"
    }

    // [{t, v}] in clip seconds, sorted. Fewer than two means a flat line.
    readonly property var keys: {
        const raw = clip.volumeKeys || []
        const out = []
        for (let i = 0; i < raw.length; ++i)
            out.push({ t: raw[i].t, v: raw[i].v })
        out.sort((a, b) => a.t - b.t)
        return out
    }
    readonly property real flatGain: keys.length > 0 ? keys[0].v : 1.0

    // Gain at clip time t, straight lines between dots (new dots are linear).
    function gainAt(t) {
        const k = keys
        if (k.length === 0) return 1.0
        if (t <= k[0].t) return k[0].v
        if (t >= k[k.length - 1].t) return k[k.length - 1].v
        for (let i = 1; i < k.length; ++i) {
            if (t <= k[i].t) {
                const a = k[i - 1], b = k[i]
                const span = Math.max(1e-6, b.t - a.t)
                return a.v + (b.v - a.v) * (t - a.t) / span
            }
        }
        return k[k.length - 1].v
    }

    function dotIndexAt(x, y) {
        if (keys.length < 2)
            return -1
        let best = -1, bestD = dotRadius + hitSlop
        for (let i = 0; i < keys.length; ++i) {
            const d = Math.hypot(xForTime(keys[i].t) - x, yForGain(keys[i].v) - y)
            if (d <= bestD) { best = i; bestD = d }
        }
        return best
    }
    function nearLine(x, y) {
        return Math.abs(yForGain(gainAt(timeForX(x))) - y) <= hitSlop
    }

    // ── drawing ──────────────────────────────────────────────────────────────────────────────
    readonly property bool active: hover.hovered || drag.dragging || owner.selected
    readonly property bool shaped: keys.length >= 2

    // Vector path, not a Canvas: a long clip can be tens of thousands of pixels wide, past any
    // texture size, and the path only holds as many points as there are dots.
    readonly property var linePoints: {
        const k = keys
        if (k.length < 2) {
            const y = yForGain(flatGain)
            return [Qt.point(0, y), Qt.point(width, y)]
        }
        const pts = [Qt.point(0, yForGain(k[0].v))]
        for (let i = 0; i < k.length; ++i)
            pts.push(Qt.point(xForTime(k[i].t), yForGain(k[i].v)))
        pts.push(Qt.point(width, yForGain(k[k.length - 1].v)))
        return pts
    }

    Shape {
        anchors.fill: parent
        visible: root.active || root.shaped
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: Theme.primary
            strokeWidth: 1.5
            fillColor: "transparent"
            PathPolyline { path: root.linePoints }
        }
    }

    Repeater {
        model: root.shaped ? root.keys : []
        delegate: Rectangle {
            required property var modelData
            required property int index
            readonly property bool hot: drag.dotIndex === index || hover.dotIndex === index
            x: root.xForTime(modelData.t) - width / 2
            y: root.yForGain(modelData.v) - height / 2
            width: hot ? root.dotRadius * 2 + 3 : root.dotRadius * 2
            height: width
            radius: width / 2
            visible: root.active
            color: hot ? Theme.primary : Theme.c.timelineFadeHandle.background
            border.color: Theme.primary
            border.width: 1.5
        }
    }

    // Level readout while dragging or pointing at a dot.
    Rectangle {
        readonly property bool show: root.active && (drag.dragging || hover.dotIndex >= 0)
        readonly property real keyT: drag.dragging ? drag.currentAbs - root.clipStart
                                     : (hover.dotIndex >= 0 && hover.dotIndex < root.keys.length
                                        ? root.keys[hover.dotIndex].t : 0)
        readonly property real keyV: root.gainAt(keyT)
        visible: show
        x: Math.max(0, Math.min(root.width - width, root.xForTime(keyT) - width / 2))
        y: root.yForGain(keyV) > height + 8 ? root.yForGain(keyV) - height - 8 : root.yForGain(keyV) + 8
        width: readout.implicitWidth + 8
        height: readout.implicitHeight + 4
        radius: 3
        color: "#d0000000"
        z: 2
        Text {
            id: readout
            anchors.centerIn: parent
            text: root.dbText(parent.keyV)
            color: "white"
            font.pixelSize: Theme.fontSizeXs
        }
    }

    // ── interaction ──────────────────────────────────────────────────────────────────────────
    // Hover is passive so the clip underneath keeps its own hover state.
    HoverHandler {
        id: hover
        property int dotIndex: hovered ? root.dotIndexAt(point.position.x, point.position.y) : -1
        readonly property bool overLine: hovered && root.nearLine(point.position.x, point.position.y)
        // undefined resets it, so away from the line the clip keeps its own cursor.
        cursorShape: dotIndex >= 0 ? Qt.SizeAllCursor : overLine ? Qt.CrossCursor : undefined
    }

    MouseArea {
        id: drag
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        preventStealing: true

        property bool dragging: false
        property bool previewOpen: false
        property int dotIndex: -1
        property real currentAbs: 0     // the dragged dot's time, absolute timeline seconds
        property real lowT: 0           // clip-time bounds for the dragged dot
        property real highT: 0
        property real pressX: 0
        property bool shiftAtPress: false

        function openPreview(label) {
            if (previewOpen)
                return
            EditorState.beginPreviewDrag(label)
            previewOpen = true
        }
        function boundsFor(i) {
            const k = root.keys
            lowT = i > 0 ? k[i - 1].t + root.minGapSeconds : 0
            highT = i < k.length - 1 ? k[i + 1].t - root.minGapSeconds : root.clipDuration
        }

        onPressed: (mouse) => {
            shiftAtPress = (mouse.modifiers & Qt.ShiftModifier) !== 0
            const hit = root.dotIndexAt(mouse.x, mouse.y)
            if (mouse.button === Qt.RightButton) {
                if (hit < 0) { mouse.accepted = false; return }
                EditorState.removeClipKeyframe(owner.trackIndex, owner.clipIndex, "volume",
                                               root.clipStart + root.keys[hit].t)
                return
            }
            if (hit >= 0) {
                // Existing dot: the edit starts only once it actually moves.
                dotIndex = hit
                currentAbs = root.clipStart + root.keys[hit].t
                boundsFor(hit)
                pressX = mouse.x
                dragging = true
                return
            }
            if (!root.nearLine(mouse.x, mouse.y)) {
                mouse.accepted = false   // the clip below selects / moves as usual
                return
            }
            // New dot on the line. A flat line gets its two ends pinned first, so the new dot
            // shapes only its neighbourhood.
            Qt.callLater(function() { owner.forceActiveFocus() })
            const t = Math.max(root.minGapSeconds, Math.min(root.clipDuration - root.minGapSeconds,
                                                            root.timeForX(mouse.x)))
            const v = root.gainAt(t)
            openPreview(qsTr("Add volume point"))
            if (root.keys.length < 2) {
                const g = root.flatGain
                EditorState.previewSetClipKeyframe(owner.trackIndex, owner.clipIndex, "volume",
                                                   root.clipStart, g)
                EditorState.previewSetClipKeyframe(owner.trackIndex, owner.clipIndex, "volume",
                                                   root.clipStart + root.clipDuration, g)
            }
            EditorState.previewSetClipKeyframe(owner.trackIndex, owner.clipIndex, "volume",
                                               root.clipStart + t, v)
            currentAbs = root.clipStart + t
            // Neighbours of the new dot, from the line as it was.
            let lo = 0, hi = root.clipDuration
            for (let i = 0; i < root.keys.length; ++i) {
                if (root.keys[i].t < t) lo = root.keys[i].t
                else if (root.keys[i].t > t) { hi = root.keys[i].t; break }
            }
            lowT = lo + root.minGapSeconds
            highT = hi - root.minGapSeconds
            dotIndex = -2   // a new dot; its index settles once the model catches up
            pressX = mouse.x
            dragging = true
        }

        onPositionChanged: (mouse) => {
            if (!dragging)
                return
            const shift = shiftAtPress || (mouse.modifiers & Qt.ShiftModifier) !== 0
            let t = shift ? currentAbs - root.clipStart : root.timeForX(mouse.x)
            // Hold the time until the pointer clearly moves sideways, so a vertical nudge does
            // not also slide the dot.
            if (!shift && Math.abs(mouse.x - pressX) < 3)
                t = currentAbs - root.clipStart
            t = Math.max(lowT, Math.min(highT, t))
            const v = root.gainForY(mouse.y)
            openPreview(qsTr("Move volume point"))
            const toAbs = root.clipStart + t
            EditorState.previewMoveClipKeyframe(owner.trackIndex, owner.clipIndex, "volume",
                                                currentAbs, toAbs, v)
            currentAbs = toAbs
        }

        onReleased: {
            if (previewOpen)
                EditorState.commitPreviewDrag()
            previewOpen = false
            dragging = false
            dotIndex = -1
        }
        onCanceled: {
            if (previewOpen)
                EditorState.cancelPreviewDrag()
            previewOpen = false
            dragging = false
            dotIndex = -1
        }
        onDoubleClicked: (mouse) => {
            const hit = root.dotIndexAt(mouse.x, mouse.y)
            if (hit < 0) {
                mouse.accepted = false
                return
            }
            // End the gesture first: a move after this must not drag the removed dot back.
            dragging = false
            dotIndex = -1
            if (previewOpen) {
                EditorState.cancelPreviewDrag()
                previewOpen = false
            }
            EditorState.removeClipKeyframe(owner.trackIndex, owner.clipIndex, "volume",
                                           root.clipStart + root.keys[hit].t)
        }
    }
}
