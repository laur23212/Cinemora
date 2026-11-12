import QtQuick

// Laid over a numeric field: press and drag sideways to scrub the value, Blender-style. A press
// that never crosses the drag threshold is a click (`activated`). Mouse only: a touch press falls
// through to the field underneath, so taps still edit and the panel still scrolls. While
// `editing`, presses go to the text field and the wheel steps the value instead of scrolling.
MouseArea {
    id: root

    property bool editing: false
    readonly property bool scrubbing: _scrubbing

    // `steps` is cumulative since the press: one per pixel, Ctrl ×10 (coarse), Shift or Alt ×0.1
    // (fine; Dafin: Shift used to be coarse). Escape mid-drag emits scrubCanceled. Modifiers apply
    // per move, so switching them mid-drag changes the rate without a jump.
    signal scrubStarted()
    signal scrubbed(real steps)
    signal scrubFinished()
    // Dafin: Escape during a drag. Emitted instead of scrubFinished(); the release that follows
    // is ignored (neither a finish nor a click).
    signal scrubCanceled()
    signal activated()
    signal wheelStepped(real steps)

    property bool _scrubbing: false
    property real _pressX: 0
    property real _lastX: 0
    property real _steps: 0
    property real _wheelAccum: 0
    property bool _canceled: false

    acceptedButtons: Qt.LeftButton
    preventStealing: true
    cursorShape: editing ? Qt.IBeamCursor : Qt.SizeHorCursor

    function _rate(modifiers) {
        if (modifiers & Qt.ControlModifier)
            return 10
        if (modifiers & (Qt.ShiftModifier | Qt.AltModifier))
            return 0.1
        return 1
    }

    function _finish() {
        if (!_scrubbing)
            return
        _scrubbing = false
        scrubFinished()
    }

    onPressed: mouse => {
        if (editing || mouse.source !== Qt.MouseEventNotSynthesized) {
            mouse.accepted = false
            return
        }
        _pressX = mouse.x
        _steps = 0
        _canceled = false
        forceActiveFocus() // so Escape reaches Keys.onEscapePressed below
    }

    Keys.onEscapePressed: event => {
        if (!_scrubbing)
            return
        _scrubbing = false
        _canceled = true
        scrubCanceled()
        event.accepted = true
    }

    // Hover-enabled areas also get moves with no button down; those must never scrub.
    onPositionChanged: mouse => {
        if (!pressed || _canceled)
            return
        if (!_scrubbing) {
            if (Math.abs(mouse.x - _pressX) < Application.styleHints.startDragDistance)
                return
            _scrubbing = true
            _lastX = mouse.x
            scrubStarted()
            return
        }
        _steps += (mouse.x - _lastX) * _rate(mouse.modifiers)
        _lastX = mouse.x
        scrubbed(_steps)
    }

    onReleased: {
        if (_canceled) {
            _canceled = false
            return
        }
        if (_scrubbing)
            _finish()
        else
            activated()
    }
    onCanceled: _finish()
    Component.onDestruction: _finish()

    onWheel: wheel => {
        if (!editing) {
            wheel.accepted = false
            return
        }
        // Shift (and Alt, on some platforms) turns the wheel horizontal.
        _wheelAccum += wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : wheel.angleDelta.x
        const notches = Math.trunc(_wheelAccum / 120)
        if (notches !== 0) {
            _wheelAccum -= notches * 120
            wheelStepped(notches * _rate(wheel.modifiers))
        }
    }
}
