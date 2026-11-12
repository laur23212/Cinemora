import QtQuick
import Drift

// Dafin: a speed ramp drawn the way Filmora draws it — speed over the length of the clip, on a
// log scale from 0.1× to 10× — either as a preset's thumbnail (interactive: false) or as the
// selected clip's ramp to edit (interactive: true):
//
//   drag a point        up/down changes the speed there, sideways moves it along the clip
//                       (the first and last points stay at the ends)
//   double-click        adds a point there
//   right-click a point removes it (the ends stay)
//
// Edits come out of pointsEdited(points, committed): `committed` is false while dragging (preview) and
// true once, on release (the undo step). Points are {pos, speed, inDx, inDy, outDx, outDy, corner},
// SpeedCurve's own JSON shape; edited curves get the same smooth flat tangents the presets have.
Item {
    id: graph

    property var points: []
    property bool interactive: false
    property color lineColor: Theme.primary
    property real lineWidth: interactive ? 2 : 1.5
    // Where the playhead is along the clip's timeline span, 0..1 (-1 hides the line).
    property real playheadFraction: -1
    property int selectedIndex: -1

    signal pointsEdited(var points, bool committed)

    readonly property real minLog: -1          // 0.1×
    readonly property real maxLog: 1           // 10×
    readonly property real minSpeed: 0.1       // drift::kMinCurveSpeed
    readonly property real maxSpeed: 8.0       // drift::kMaxCurveSpeed
    readonly property real padX: interactive ? 8 : 4
    readonly property real padTop: interactive ? 8 : 4
    readonly property real padBottom: interactive ? 8 : 4

    function xFor(pos) { return padX + pos * Math.max(1, width - 2 * padX) }
    function posForX(x) { return Math.max(0, Math.min(1, (x - padX) / Math.max(1, width - 2 * padX))) }
    function yFor(speed) {
        const l = Math.log(Math.max(0.1, Math.min(10, speed))) / Math.LN10
        return padTop + (1 - (l - minLog) / (maxLog - minLog)) * Math.max(1, height - padTop - padBottom)
    }
    function speedForY(y) {
        const f = 1 - (y - padTop) / Math.max(1, height - padTop - padBottom)
        const s = Math.pow(10, minLog + f * (maxLog - minLog))
        return Math.max(minSpeed, Math.min(maxSpeed, s))
    }

    function bez(a, b, c, d, t) {
        const u = 1 - t
        return u * u * u * a + 3 * u * u * t * b + 3 * u * t * t * c + t * t * t * d
    }

    // Speed at `pos` (0..1): the segment's cubic, its parameter found by bisection on x.
    function speedAt(pos) {
        const p = graph.points
        if (!p || p.length < 2)
            return 1
        let i = 0
        while (i + 2 < p.length && p[i + 1].pos < pos)
            ++i
        const a = p[i], b = p[i + 1]
        const x0 = a.pos, x1 = a.pos + (a.outDx || 0), x2 = b.pos + (b.inDx || 0), x3 = b.pos
        if (x3 - x0 <= 1e-9)
            return b.speed
        let lo = 0, hi = 1
        for (let k = 0; k < 24; ++k) {
            const mid = (lo + hi) / 2
            if (bez(x0, x1, x2, x3, mid) < pos)
                lo = mid
            else
                hi = mid
        }
        const t = (lo + hi) / 2
        return bez(a.speed, a.speed + (a.outDy || 0), b.speed + (b.inDy || 0), b.speed, t)
    }

    // The source position the clip has reached a `fraction` of the way through its timeline span:
    // the curve's timeline is the integral of 1/speed, inverted by table lookup.
    function posForTimelineFraction(fraction) {
        const n = 128
        const cum = [0]
        for (let i = 1; i <= n; ++i) {
            const s0 = Math.max(minSpeed, speedAt((i - 1) / n))
            const s1 = Math.max(minSpeed, speedAt(i / n))
            cum.push(cum[i - 1] + 0.5 * (1 / s0 + 1 / s1) / n)
        }
        const target = Math.max(0, Math.min(1, fraction)) * cum[n]
        for (let i = 1; i <= n; ++i) {
            if (cum[i] >= target) {
                const span = cum[i] - cum[i - 1]
                const f = span > 0 ? (target - cum[i - 1]) / span : 0
                return (i - 1 + f) / n
            }
        }
        return 1
    }

    // Flat tangents a third of the way to each neighbour (SpeedRamp.cpp smoothRamp); a corner point
    // keeps sharp ones.
    function smoothed(list) {
        const out = []
        for (let i = 0; i < list.length; ++i) {
            const q = list[i]
            const prev = i > 0 ? list[i - 1] : null
            const next = i + 1 < list.length ? list[i + 1] : null
            out.push({
                pos: q.pos,
                speed: q.speed,
                inDx: prev && !q.corner ? -(q.pos - prev.pos) / 3 : 0,
                inDy: 0,
                outDx: next && !q.corner ? (next.pos - q.pos) / 3 : 0,
                outDy: 0,
                corner: !!q.corner
            })
        }
        return out
    }

    function copyPoints() {
        const out = []
        for (let i = 0; i < graph.points.length; ++i)
            out.push(Object.assign({}, graph.points[i]))
        return out
    }

    onPointsChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()
    onLineColorChanged: canvas.requestPaint()

    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            if (graph.interactive) {
                // 10×, 5×, 1×, 0.5×, 0.1× guides, as on Filmora's graph.
                ctx.font = "9px sans-serif"
                const marks = [10, 5, 1, 0.5, 0.1]
                for (let m = 0; m < marks.length; ++m) {
                    const y = Math.round(graph.yFor(marks[m])) + 0.5
                    ctx.strokeStyle = marks[m] === 1 ? Qt.rgba(1, 1, 1, 0.28) : Qt.rgba(1, 1, 1, 0.12)
                    ctx.lineWidth = 1
                    ctx.setLineDash(marks[m] === 1 ? [] : [3, 3])
                    ctx.beginPath()
                    ctx.moveTo(graph.padX, y)
                    ctx.lineTo(graph.width - graph.padX, y)
                    ctx.stroke()
                    ctx.fillStyle = Qt.rgba(1, 1, 1, 0.45)
                    ctx.fillText(marks[m] + "x", graph.width - graph.padX - 22, y - 2)
                }
                ctx.setLineDash([])
            }
            const p = graph.points
            ctx.strokeStyle = graph.lineColor
            ctx.lineWidth = graph.lineWidth
            ctx.beginPath()
            if (!p || p.length < 2) {
                // No ramp: the clip plays at one speed, drawn as a flat line at 1×.
                ctx.moveTo(graph.xFor(0), graph.yFor(1))
                ctx.lineTo(graph.xFor(1), graph.yFor(1))
            } else {
                const steps = Math.max(24, Math.round(graph.width / 3))
                for (let k = 0; k <= steps; ++k) {
                    const pos = k / steps
                    const x = graph.xFor(pos)
                    const y = graph.yFor(graph.speedAt(pos))
                    if (k === 0)
                        ctx.moveTo(x, y)
                    else
                        ctx.lineTo(x, y)
                }
            }
            ctx.stroke()
        }
    }

    // The playhead, where the clip is along its ramp.
    Rectangle {
        visible: graph.interactive && graph.playheadFraction >= 0 && graph.playheadFraction <= 1
                 && graph.points.length >= 2
        x: Math.round(graph.xFor(graph.posForTimelineFraction(graph.playheadFraction)))
        y: graph.padTop
        width: 1
        height: graph.height - graph.padTop - graph.padBottom
        color: "#ff5a5a"
    }

    // Double-click adds a point where it lands.
    MouseArea {
        anchors.fill: parent
        enabled: graph.interactive && graph.points.length >= 2
        acceptedButtons: Qt.LeftButton
        onDoubleClicked: (mouse) => {
            const pos = graph.posForX(mouse.x)
            const list = graph.copyPoints()
            let at = list.length - 1
            for (let i = 1; i < list.length; ++i) {
                if (list[i].pos > pos) {
                    at = i
                    break
                }
            }
            if (pos - list[at - 1].pos < 0.02 || list[at].pos - pos < 0.02)
                return
            list.splice(at, 0, { pos: pos, speed: graph.speedForY(mouse.y), corner: false })
            graph.selectedIndex = at
            graph.pointsEdited(graph.smoothed(list), true)
        }
        onClicked: graph.selectedIndex = -1
    }

    Repeater {
        model: graph.interactive ? graph.points.length : 0
        delegate: Rectangle {
            id: knob
            required property int index
            readonly property var point: graph.points[index] || ({ pos: 0, speed: 1 })
            readonly property bool selected: graph.selectedIndex === index
            width: 11
            height: 11
            radius: 5.5
            x: graph.xFor(point.pos) - width / 2
            y: graph.yFor(point.speed) - height / 2
            color: selected || knobMouse.pressed ? "white" : "#d8d8d8"
            border.width: selected ? 2 : 1
            border.color: selected ? graph.lineColor : "#1e1e1e"

            ThemedToolTip {
                visible: knobMouse.containsMouse || knobMouse.pressed
                text: knob.point.speed.toFixed(2) + "×"
            }

            MouseArea {
                id: knobMouse
                anchors.fill: parent
                anchors.margins: -5
                hoverEnabled: true
                preventStealing: true
                cursorShape: Qt.SizeAllCursor
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                property bool moved: false
                onPressed: (mouse) => {
                    graph.selectedIndex = knob.index
                    moved = false
                    if (mouse.button === Qt.RightButton) {
                        const list = graph.copyPoints()
                        if (knob.index > 0 && knob.index < list.length - 1) {
                            list.splice(knob.index, 1)
                            graph.selectedIndex = -1
                            graph.pointsEdited(graph.smoothed(list), true)
                        }
                    }
                }
                onPositionChanged: (mouse) => {
                    if (!pressed || !(pressedButtons & Qt.LeftButton))
                        return
                    const local = mapToItem(graph, mouse.x, mouse.y)
                    const list = graph.copyPoints()
                    const i = knob.index
                    const q = list[i]
                    q.speed = graph.speedForY(local.y)
                    if (i > 0 && i < list.length - 1) {
                        q.pos = Math.max(list[i - 1].pos + 0.02,
                                         Math.min(list[i + 1].pos - 0.02, graph.posForX(local.x)))
                    }
                    moved = true
                    graph.pointsEdited(graph.smoothed(list), false)
                }
                onReleased: (mouse) => {
                    if (moved && mouse.button === Qt.LeftButton)
                        graph.pointsEdited(graph.smoothed(graph.copyPoints()), true)
                    moved = false
                }
            }
        }
    }
}
