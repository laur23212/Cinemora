import QtQuick
import Drift
// Dafin: live preview on hover. Rest the pointer on an effect or effect stack in the library and
// the preview window shows it on the clip under the playhead, without applying it. Leaving the
// card, starting a drag, pressing play or any edit (including applying the preset) puts the clip
// back. Nothing here is an edit, so nothing is undoable and the project is not dirtied.
//
// Put one inside a card: `PresetHoverPreview { kind: "effect"; payload: id; hovered: hover.hovered }`.
Item {
    id: root

    // The AssetDrag kind ("effect" or "effectStack") and the preset id the card drags.
    property string kind: ""
    property string payload: ""
    property bool hovered: false
    // True while the card is being dragged or held: a drag is about to apply it for real.
    property bool suspended: false
    // Wait for the pointer to settle, so sweeping across the grid does not trigger a render of
    // every card passed on the way.
    property int delayMs: 200

    // Whether this card is the one currently showing a trial.
    property bool shown: false

    visible: false
    width: 0
    height: 0

    function stop() {
        showTimer.stop()
        if (shown) {
            shown = false
            EditorState.clearPresetPreview()
        }
    }

    onHoveredChanged: {
        if (hovered && !suspended && kind.length > 0 && payload.length > 0)
            showTimer.restart()
        else
            stop()
    }
    onSuspendedChanged: {
        if (suspended)
            stop()
    }
    Component.onDestruction: stop()

    Timer {
        id: showTimer
        interval: root.delayMs
        onTriggered: root.shown = EditorState.previewPresetOnPlayhead(root.kind, root.payload)
    }

    Connections {
        target: EditorState
        // Applying the preset (or any other edit) is an undo step: drop the trial so it never
        // doubles up with the real effect.
        function onUndoStackChanged() { root.stop() }
        function onPlayingChanged() { if (EditorState.playing) root.stop() }
    }
}
