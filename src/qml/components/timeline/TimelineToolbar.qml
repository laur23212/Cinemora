import QtQuick
import Drift

// Timeline toolbar: transport, user-arranged edit actions with a More menu, timeline
// switcher (main / composites), snap/ripple toggles, zoom controls and the overview toggle. Zoom and the
// time readout are read from and written back to the owning TimelinePanel via `panel`.
// New tracks are added from the plus button above the track headers.
Item {
    id: toolbar

    // Owning TimelinePanel; provides zoom (read/write via setZoom), zoom bounds and formatTime.
    property var panel

    height: Theme.timelineToolbarHeight

    // Appends an action's current binding to its tooltip. Every action here has one,
    // but only the header's Save button used to show it, so the keyboard route to
    // anything on this toolbar was undiscoverable. Rebound keys follow automatically
    // because shortcutFor reads the live map.
    function withShortcut(label, actionId) {
        const key = EditorState.shortcutFor(actionId)
        return key.length > 0 ? qsTr("%1 (%2)").arg(label).arg(key) : label
    }

    ThemeSlot {
        slot: "TimelineToolbarBackground"
        control: toolbar
        anchors.fill: parent
        z: -1
        fallback: Component {
            Rectangle {
                anchors.fill: parent
                color: Theme.c.timelineToolbar.background
            }
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.c.timelineToolbar.border
    }

    TimelineToolbarActions {
        id: actions
    }

    readonly property var actionMeta: actions.actionMeta
    readonly property var layout: actions.layout

    function actionTooltip(id) {
        const meta = actionMeta[id]
        let label = meta.tip || meta.label
        if (id === "keyframe" && actionActive("keyframe"))
            label = qsTr("Remove keyframe — the playhead is on a keyframe of the selected clip")
        else if (id === "chromaKey" && toolbar.chromaKeyState.present)
            label = toolbar.chromaKeyState.enabled
                    ? qsTr("Chroma key is on for the selected clip — click to turn it off. Its settings are in Effects")
                    : qsTr("Chroma key is off for the selected clip — click to turn it back on")
        else if (id === "motionTrack" && toolbar.selectedFollows)
            label = qsTr("Motion tracking — the selected element follows a point. Click to correct the point, pick a new one or stop")
        else if (id === "syncAudio" && EditorState.audioAlign.running)
            label = qsTr("Sync audio — listening to the clips… %1%").arg(Math.round(EditorState.audioAlign.progress * 100))
        return meta.shortcut ? withShortcut(label, meta.shortcut) : label
    }

    // Dafin: the selected clip's chroma key ({eligible, present, enabled}): the button lights up
    // while the key is on.
    readonly property var chromaKeyState: (EditorState.selectedClipData, EditorState.tracksRevision,
        EditorState.selectedTrack >= 0 && EditorState.selectedClip >= 0
        ? EditorState.chromaKeyInfo(EditorState.selectedTrack, EditorState.selectedClip)
        : ({ eligible: false, present: false, enabled: false }))
    // ...and the selected element follows a tracked point: the motion tracking button lights up.
    readonly property bool selectedFollows: (EditorState.tracksRevision,
        !!EditorState.selectedClipData.id
        && EditorState.tracking.motionInfo(EditorState.selectedClipData.id).following === true)

    // The playhead sits on a keyframe of the selected clip: the keyframe button lights up and
    // a click removes that keyframe instead of adding one (as Filmora's diamonds do).
    readonly property bool keyframeAtPlayhead: (EditorState.selectedClipData, EditorState.playheadSeconds,
        EditorState.selectedTrack >= 0 && EditorState.selectedClip >= 0
        && EditorState.clipHasKeyframeAt(EditorState.selectedTrack, EditorState.selectedClip,
                                         EditorState.playheadSeconds))

    function triggerAction(id) {
        switch (id) {
        case "select": panel.timelineTool = ""; break
        case "cut": panel.timelineTool = panel.timelineTool === "split" ? "" : "split"; break
        case "keyframe": EditorState.toggleKeyframeAtPlayhead(); break
        case "snap": EditorState.snapEnabled = !EditorState.snapEnabled; break
        case "chromaKey": EditorState.toggleChromaKey(-1, -1); break
        case "motionTrack": ShellBridge.motionTrackingRequested(); break
        case "syncAudio":
            if (EditorState.audioAlign.running)
                EditorState.audioAlign.cancel()
            else
                EditorState.audioAlign.syncSimilarAudio()
            break
        case "trimStart": panel.timelineTool = panel.timelineTool === "trimStart" ? "" : "trimStart"; break
        case "trimEnd": panel.timelineTool = panel.timelineTool === "trimEnd" ? "" : "trimEnd"; break
        case "deleteLeft": EditorState.splitSelectedClipLeft(); break
        case "deleteRight": EditorState.splitSelectedClipRight(); break
        case "separateAudio": EditorState.separateAudioFromSelection(); break
        case "unlink": EditorState.unlinkSelectedClips(); break
        case "link": EditorState.linkSelectedClips(); break
        case "undo": EditorState.undo(); break
        case "redo": EditorState.redo(); break
        case "delete": EditorState.deleteSelectedClip(); break
        case "copy": EditorState.copySelection(); break
        case "paste": EditorState.pasteAtPlayhead(); break
        case "duplicate": EditorState.duplicateSelectedClip(); break
        case "bookmark": EditorState.toggleBookmarkAtPlayhead(); break
        case "markIn": EditorState.markWorkAreaIn(); break
        case "markOut": EditorState.markWorkAreaOut(); break
        case "loop": EditorState.toggleLoopWorkArea(); break
        case "clearWorkArea": EditorState.clearWorkArea(); break
        case "merge": EditorState.mergeSelectedClips(); break
        case "freeze": EditorState.freezeFrameAtPlayhead(); break
        case "adjustmentLayer": EditorState.addAdjustmentClip(-1, -1); break
        case "transformLayer": EditorState.addTransformLayerForSelection(); break
        case "camera": EditorState.addCameraTrack(); break
        }
    }

    function actionEnabled(id) {
        switch (id) {
        case "separateAudio": return EditorState.separateAudioAvailable
        case "unlink": return EditorState.unlinkAvailable
        case "link": return EditorState.linkAvailable
        case "undo": return EditorState.undoAvailable
        case "redo": return EditorState.redoAvailable
        case "loop": return EditorState.workAreaActive
        case "clearWorkArea": return EditorState.workAreaInSeconds >= 0
                                     || EditorState.workAreaOutSeconds >= 0
        case "merge": return EditorState.mergeAvailable
        }
        return true
    }

    function actionActive(id) {
        switch (id) {
        case "select": return panel.timelineTool === ""
        case "cut": return panel.timelineTool === "split"
        case "keyframe": return toolbar.keyframeAtPlayhead
        case "snap": return EditorState.snapEnabled
        case "chromaKey": return toolbar.chromaKeyState.enabled === true
        case "motionTrack": return !!ShellBridge.motionPick || toolbar.selectedFollows
        case "syncAudio": return EditorState.audioAlign.running
        case "trimStart": return panel.timelineTool === "trimStart"
        case "trimEnd": return panel.timelineTool === "trimEnd"
        case "markIn": return EditorState.workAreaInSeconds >= 0
        case "markOut": return EditorState.workAreaOutSeconds >= 0
        case "loop": return EditorState.loopWorkAreaEnabled
        }
        return false
    }

    // On the toolbar the A/V pair only appears when it applies to the selection, so a
    // selection shows one or the other rather than two mostly-disabled buttons.
    function actionShownOnToolbar(id) {
        if (id === "separator")
            return true
        if (id === "separateAudio")
            return EditorState.separateAudioAvailable
        if (id === "unlink")
            return EditorState.unlinkAvailable
        if (id === "link")
            return EditorState.linkAvailable
        return true
    }

    // A separator only divides something: not at either end, and not next to another one
    // (which happens when the buttons between them are all contextually hidden).
    function separatorNeeded(list, i, shown) {
        var before = false
        for (var b = i - 1; b >= 0 && list[b] !== "separator"; b--) {
            if (shown(list[b])) { before = true; break }
        }
        if (!before)
            return false
        for (var a = i + 1; a < list.length; a++) {
            if (list[a] === "separator")
                return false
            if (shown(list[a]))
                return true
        }
        return false
    }


    TimelineToolbarHairline { }

    // Long-press and drag rearranges the toolbar. The fixed controls (import, play, timecode on
    // the left; the toggles and zoom on the right) move among themselves; the edit actions keep
    // to their own run because they are stored with the More menu, and dropping one on the More
    // button moves it into that menu.
    BarReorder {
        id: timelineReorder
        defaults: ({
            "start": ["import", "separatorA", "play", "timecode", "separatorB"],
            // Dafin: snapping moved to the edit actions, beside Select, Cut mode and Keyframe.
            "end": ["overlap", "autoSelect", "separatorC", "ripple", "zoom", "separatorD",
                    "mixer", "overview"]
        })
        stored: EditorState.preferences.barLayouts["timeline"]
        layer: reorderLayer
        onSaveRequested: (layout) => EditorState.preferences.setBarLayout("timeline", layout)
    }

    // The edit actions repeat "separator", so their drag ids carry the position: "separator#4".
    function actionDragId(id, index) {
        return id === "separator" ? "separator#" + index : id
    }
    function actionFromDragId(id) {
        return id.indexOf("separator#") === 0 ? "separator" : id
    }

    BarReorder {
        id: actionsReorder
        readonly property var tagged: toolbar.layout.toolbar.map((id, i) => toolbar.actionDragId(id, i))
        defaults: ({ "actions": tagged })
        stored: ({ "actions": tagged })
        layer: actionsReorderLayer
        extraTargets: [{
            item: overflowButton,
            groups: ["actions"],
            drop: function (id, fromGroup) {
                const index = actionsReorder.tagged.indexOf(id)
                if (index < 0)
                    return
                const toolbarItems = toolbar.layout.toolbar.slice()
                toolbarItems.splice(index, 1)
                const menuItems = [toolbar.actionFromDragId(id)].concat(toolbar.layout.menu)
                EditorState.preferences.setTimelineToolbarLayout(toolbarItems, menuItems)
            }
        }]
        onSaveRequested: (layout) => EditorState.preferences.setTimelineToolbarLayout(
                             layout["actions"].map((id) => toolbar.actionFromDragId(id)),
                             toolbar.layout.menu)
    }

    readonly property var barItems: ({
        "import": importComponent,
        "separatorA": separatorComponent,
        "play": playComponent,
        "timecode": timecodeComponent,
        "separatorB": separatorComponent,
        "overlap": overlapComponent,
        "autoSelect": autoSelectComponent,
        "separatorC": separatorComponent,
        "ripple": rippleComponent,
        "zoom": zoomComponent,
        "separatorD": separatorComponent,
        "mixer": mixerComponent,
        "overview": overviewComponent
    })

    Component { id: separatorComponent; TimelineToolbarSeparator { } }

    Component {
        id: importComponent
        ThemedButton {
            variant: "primary"
            glyph: Theme.icons.plus
            text: qsTr("Import media")
            tooltip: qsTr("Add video or audio files directly to the timeline at the playhead")
            height: Math.min(Theme.controlHeightSm, toolbar.height - Theme.spacingXs * 2)
            topPadding: Theme.spacingXs
            bottomPadding: Theme.spacingXs
            enabled: !MediaImport.importing
            onClicked: toolbar.panel.importMediaDirectlyToTimeline()
        }
    }

    Component {
        id: playComponent
        TimelineToolbarToolButton {
            glyph: EditorState.playing ? Theme.icons.pause : Theme.icons.play
            tooltip: EditorState.playing ? qsTr("Pause") : qsTr("Play")
            onClicked: EditorState.togglePlayback()
        }
    }

    Component {
        id: timecodeComponent
        TimelineToolbarTimecode {
            panel: toolbar.panel
        }
    }

    Component {
        id: overlapComponent
        TimelineToolbarToolButton {
            glyph: Theme.icons.option
            tooltip: qsTr("Allow clip overlap")
            active: EditorState.allowClipOverlap
            onClicked: EditorState.allowClipOverlap = !EditorState.allowClipOverlap
        }
    }

    Component {
        id: autoSelectComponent
        TimelineToolbarToolButton {
            glyph: Theme.icons.locateFixed
            tooltip: qsTr("Auto-select: select the clip under the playhead")
            active: EditorState.preferences.autoSelectUnderPlayhead
            onClicked: EditorState.preferences.autoSelectUnderPlayhead = !EditorState.preferences.autoSelectUnderPlayhead
        }
    }

    // Keep the ripple tool next to zoom by default, where CapCut users expect it.
    Component {
        id: rippleComponent
        TimelineToolbarToolButton {
            glyph: Theme.icons.foldHorizontal
            tooltip: qsTr("Ripple editing: keep following clips together when trimming")
            active: EditorState.rippleEnabled
            onClicked: EditorState.rippleEnabled = !EditorState.rippleEnabled
        }
    }

    Component {
        id: zoomComponent
        TimelineToolbarZoomControl {
            host: toolbar
        }
    }

    Component {
        id: mixerComponent
        TimelineToolbarToolButton {
            glyph: Theme.icons.slidersVertical
            tooltip: qsTr("Toggle audio mixer strip")
            active: EditorState.preferences.audioMixerVisible
            onClicked: EditorState.preferences.audioMixerVisible = !EditorState.preferences.audioMixerVisible
        }
    }

    Component {
        id: overviewComponent
        TimelineToolbarToolButton {
            glyph: Theme.icons.panelTop
            tooltip: qsTr("Timeline overview — a minimap of the whole project; click or drag it to jump the view")
            active: EditorState.preferences.timelineOverviewVisible
            onClicked: EditorState.preferences.timelineOverviewVisible = !EditorState.preferences.timelineOverviewVisible
        }
    }

    Row {
        id: leftControls
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingLg
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingXs
        // Never runs under the right-hand controls; buttons past the
        // available width are clipped rather than overlapping. Worked out from
        // widths, not x: under RTL mirroring the two groups swap edges.
        width: Math.max(0, toolbar.width - rightControls.width - Theme.spacingLg * 3)
        clip: true

        Row {
            id: startRow
            height: toolbar.height
            spacing: Theme.spacingXs
            Component.onCompleted: timelineReorder.registerGroup("start", startRow)
            Repeater {
                model: timelineReorder.layout["start"]
                delegate: BarSlot { reorder: timelineReorder; group: "start"; items: toolbar.barItems }
            }
        }

        Row {
            id: actionsRow
            height: toolbar.height
            spacing: Theme.spacingXs
            Component.onCompleted: actionsReorder.registerGroup("actions", actionsRow)
            Repeater {
                model: toolbar.layout.toolbar
                delegate: TimelineToolbarActionSlot {
                    host: toolbar
                    reorder: actionsReorder
                }
            }
        }

        TimelineToolbarOverflowButton {
            id: overflowButton
            host: toolbar
            anchors.verticalCenter: parent.verticalCenter
            onCustomizeRequested: customizeDialog.open()
        }
    }

    TimelineToolbarCustomizeDialog {
        id: customizeDialog
    }

    TimelineSequenceTabs {
        leftGroup: leftControls
        rightGroup: rightControls
    }

    Row {
        id: rightControls
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingLg
        anchors.verticalCenter: parent.verticalCenter
        height: toolbar.height
        spacing: Theme.spacingSm
        Component.onCompleted: timelineReorder.registerGroup("end", rightControls)

        Repeater {
            model: timelineReorder.layout["end"]
            delegate: BarSlot { reorder: timelineReorder; group: "end"; items: toolbar.barItems }
        }
    }

    BarReorderLayer {
        id: reorderLayer
        reorder: timelineReorder
    }

    BarReorderLayer {
        id: actionsReorderLayer
        reorder: actionsReorder
    }
}
