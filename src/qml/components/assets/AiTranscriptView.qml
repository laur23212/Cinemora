import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin: the transcript of the clip in focus (EditorState.ai), one line per row, in timeline
// order. Click a line to put the playhead there. Double-click it, or press its pencil, to correct
// the words: Enter keeps the change, Esc drops it, and the captions made from those words follow.
// The line being said is lit, and kept in view while playing. The host puts its controls in
// `header`, so the whole tool scrolls as one.
ListView {
    id: view

    // The row being edited, or -1. While one is open the list is not rebuilt under it.
    property int editingIndex: -1
    property bool reloadOwed: false
    property string shownClipId: ""
    // The row to keep in view when an edit changes how many lines there are.
    property int keepIndex: -1
    property int followIndex: -1
    // Past an hour the times read h:mm:ss, so the time column widens for every row alike.
    readonly property bool longTimes: lineModel.count > 0
                                      && lineModel.get(lineModel.count - 1).time.split(":").length > 2

    clip: true
    spacing: 2
    boundsBehavior: Flickable.StopAtBounds
    model: lineModel
    ScrollBar.vertical: AppScrollBar { }

    ListModel {
        id: lineModel
    }

    // The line being said at `seconds` on the timeline, or -1.
    function lineAt(seconds) {
        let lo = 0
        let hi = lineModel.count - 1
        let found = -1
        while (lo <= hi) {
            const mid = (lo + hi) >> 1
            if (lineModel.get(mid).start <= seconds) {
                found = mid
                lo = mid + 1
            } else {
                hi = mid - 1
            }
        }
        return found >= 0 && seconds < lineModel.get(found).end ? found : -1
    }

    // Rows that changed are updated in place, so the list keeps its scroll position and an edit
    // shows where it was made.
    function sync() {
        const rows = EditorState.ai.transcriptLines
        const clipId = EditorState.ai.transcriptClipId
        const otherClip = clipId !== view.shownClipId
        view.shownClipId = clipId
        if (otherClip || rows.length !== lineModel.count) {
            const keep = view.keepIndex >= 0 ? view.keepIndex
                                             : view.indexAt(view.width / 2, view.contentY + view.height / 3)
            lineModel.clear()
            for (let i = 0; i < rows.length; ++i)
                lineModel.append(rows[i])
            view.followIndex = -1
            if (otherClip)
                view.positionViewAtBeginning()
            else if (keep >= 0 && lineModel.count > 0)
                view.positionViewAtIndex(Math.min(keep, lineModel.count - 1), ListView.Contain)
            view.keepIndex = -1
            return
        }
        for (let j = 0; j < rows.length; ++j) {
            const a = rows[j]
            const b = lineModel.get(j)
            if (a.text !== b.text || a.start !== b.start || a.end !== b.end || a.time !== b.time
                    || a.speaker !== b.speaker)
                lineModel.set(j, a)
        }
        view.keepIndex = -1
    }

    function endEdit() {
        view.editingIndex = -1
        if (view.reloadOwed) {
            view.reloadOwed = false
            view.sync()
        }
    }

    Component.onCompleted: sync()

    Connections {
        target: EditorState.ai
        function onTranscriptChanged() {
            if (view.editingIndex >= 0)
                view.reloadOwed = true
            else
                view.sync()
        }
    }

    Connections {
        target: EditorState
        function onPlayheadSecondsChanged() {
            if (!EditorState.playing || view.editingIndex >= 0 || view.moving || lineModel.count === 0)
                return
            const i = view.lineAt(EditorState.playheadSeconds)
            if (i >= 0 && i !== view.followIndex) {
                view.followIndex = i
                view.positionViewAtIndex(i, ListView.Contain)
            }
        }
    }

    delegate: Rectangle {
        id: line

        required property int index
        required property string text
        required property real start
        required property real end
        required property string time
        required property string speaker

        // A row being removed reports index -1, which must never match "no row is being edited".
        readonly property bool editing: line.index >= 0 && view.editingIndex === line.index
        readonly property bool current: EditorState.playheadSeconds >= line.start
                                        && EditorState.playheadSeconds < line.end

        function startEdit() {
            if (view.editingIndex >= 0 && view.editingIndex !== line.index)
                return
            view.editingIndex = line.index
            editor.text = line.text
            editor.forceActiveFocus()
            editor.cursorPosition = editor.length
        }

        function commit() {
            if (!line.editing)
                return
            const typed = editor.text.replace(/\s+/g, " ").trim()
            view.keepIndex = line.index
            const index = line.index
            view.endEdit()
            // Even unchanged words go through: captions an undo left behind catch up with them.
            EditorState.ai.setLineText(index, typed)
        }

        function cancelEdit() {
            if (line.editing)
                view.endEdit()
        }

        width: view.width
        height: body.y + body.height + Theme.spacingSm
        radius: Theme.c.scenesShotRow.radius
        color: lineHover.hovered && !line.editing ? Theme.c.scenesShotRow.backgroundHover : "transparent"

        Accessible.role: Accessible.ListItem
        Accessible.name: line.time + " " + line.text

        // The line being said.
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: Theme.c.subtitlesTab.accent
            opacity: line.current ? 0.14 : 0
        }

        Text {
            id: timeLabel
            x: Theme.spacingSm
            y: Theme.spacingSm
            width: timeMetrics.advanceWidth + Theme.spacingSm
            text: line.time
            color: line.current ? Theme.c.subtitlesTab.accent : Theme.c.subtitlesTab.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        TextMetrics {
            id: timeMetrics
            font: timeLabel.font
            text: view.longTimes ? "0:00:00" : "00:00"
        }

        Column {
            id: body
            anchors.left: timeLabel.right
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingSm + (pencil.visible ? pencil.width : 0)
            y: Theme.spacingSm
            spacing: 2

            Text {
                width: parent.width
                visible: line.speaker.length > 0
                text: line.speaker
                color: Theme.c.subtitlesTab.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Text {
                id: bodyText
                width: parent.width
                visible: !line.editing
                text: line.text
                wrapMode: Text.Wrap
                color: Theme.c.subtitlesTab.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
            }

            ThemedTextArea {
                id: editor
                width: parent.width
                visible: line.editing
                wrapMode: TextArea.Wrap
                Accessible.name: qsTr("Transcript line")

                Keys.onReturnPressed: (event) => {
                    if (event.modifiers & Qt.ShiftModifier) {
                        event.accepted = false
                        return
                    }
                    event.accepted = true
                    line.commit()
                }
                Keys.onEnterPressed: (event) => {
                    event.accepted = true
                    line.commit()
                }
                Keys.onEscapePressed: (event) => {
                    event.accepted = true
                    line.cancelEdit()
                }
                // Clicking elsewhere keeps what was typed, as the other inline editors do.
                onActiveFocusChanged: {
                    if (!activeFocus && line.editing)
                        line.commit()
                }
            }
        }

        IconButton {
            id: pencil
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: Theme.spacingXs
            anchors.topMargin: Theme.spacingXs
            visible: lineHover.hovered && !line.editing && view.editingIndex < 0
            glyph: Theme.icons.pencil
            buttonSize: Theme.controlHeightSm
            iconSize: Theme.iconSizeSm
            tooltip: qsTr("Correct this line")
            Accessible.name: qsTr("Correct this line")
            onClicked: line.startEdit()
        }

        HoverHandler {
            id: lineHover
            cursorShape: line.editing ? Qt.IBeamCursor : Qt.PointingHandCursor
        }

        TapHandler {
            enabled: !line.editing
            onTapped: EditorState.ai.seekToLine(line.index)
            onDoubleTapped: line.startEdit()
        }
    }
}
