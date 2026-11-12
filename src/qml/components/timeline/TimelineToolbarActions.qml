import QtQuick
import Drift

// The timeline toolbar's action catalogue and layout: what each customisable action is
// called and drawn with, the default arrangement, and the stored layout cleaned against
// it. Shared by TimelineToolbar and the toolbar customizer in Settings.
QtObject {
    id: actions

    // Every customisable edit action, keyed by the id stored in the toolbar layout.
    // `tip` is the longer toolbar tooltip where the menu label alone would be too terse.
    readonly property var actionMeta: ({
        "select": { glyph: Theme.icons.mousePointer, label: qsTr("Select"),
                    tip: qsTr("Select — normal editing"), shortcut: "selectTool" },
        "cut": { glyph: Theme.icons.scissors, label: qsTr("Cut mode"),
                 tip: qsTr("Cut mode — click a clip to split it"), shortcut: "bladeTool" },
        // Dafin: Filmora's keyframe button. Keys the selected clip at the playhead; its diamonds
        // then show on the clip, where they can be dragged or right-clicked.
        "keyframe": { glyph: Theme.icons.diamondPlus, label: qsTr("Add keyframe"),
                      tip: qsTr("Add keyframe — keys position, scale, rotation and opacity of the selected clip at the playhead. Move the playhead and change the clip to animate it"),
                      shortcut: "addKeyframe" },
        // Dafin: Filmora keeps its snapping magnet with the edit tools, so it sits here rather
        // than with the toggles at the right end.
        "snap": { glyph: Theme.icons.magnet, label: qsTr("Snapping"),
                  tip: qsTr("Snapping — clips, cuts and the playhead line up with each other as you drag. Hold Alt to turn it off while you drag"),
                  shortcut: "toggleSnap" },
        // Dafin: one-click chroma key, motion tracking that follows the element as it is moved
        // or trimmed, and syncing clips recorded together by their sound.
        "chromaKey": { glyph: Theme.icons.chromaKey, label: qsTr("Chroma key"),
                       tip: qsTr("Chroma key — takes out the green or blue background of the selected clip, with the colour read from the frame at the playhead. Click again to turn it off or on"),
                       shortcut: "chromaKey" },
        "motionTrack": { glyph: Theme.icons.motionTrack, label: qsTr("Motion tracking"),
                         tip: qsTr("Motion tracking — select a title, sticker, Lottie or image over a video, click this, then click the spot in the video it should follow. Only the part of the video the element covers is tracked, and the tracking keeps up as you move, trim or extend it"),
                         shortcut: "motionTrack" },
        "syncAudio": { glyph: Theme.icons.audioSync, label: qsTr("Sync audio"),
                       tip: qsTr("Sync audio — lines up clips recorded at the same time, such as a camera and separate microphones, by their sound. The video stays put and the recordings move to match it. Select the clips, or just the video"),
                       shortcut: "syncAudio" },
        // Dafin: bookmarks on the beats of the selected music, and the music lowered under speech
        // as dots on its volume line.
        "beatMarkers": { glyph: Theme.icons.beatMarkers, label: qsTr("Beat markers"),
                         tip: qsTr("Beat markers — select the music and click: a marker lands on every beat, so cuts and clips snap to the music. Click again to remove them"),
                         shortcut: "beatMarkers" },
        "autoDuck": { glyph: Theme.icons.autoDuck, label: qsTr("Auto ducking"),
                      tip: qsTr("Auto ducking — select the music and click to turn it on: it goes quieter wherever a voice on another track speaks. The dips are dots on its volume line, so you can drag them afterwards. Click again to turn it off"),
                      shortcut: "autoDuck" },
        "separateAudio": { glyph: Theme.icons.audioLines, label: qsTr("Show audio on separate track"),
                           shortcut: "separateAudio" },
        "unlink": { glyph: Theme.icons.unlink, label: qsTr("Unlink video and audio"),
                    shortcut: "unlink" },
        "link": { glyph: Theme.icons.link, label: qsTr("Link selected clips"), shortcut: "link" },
        "trimStart": { glyph: Theme.icons.trimStart, label: qsTr("Trim start"),
                       tip: qsTr("Trim start — click a clip to drop everything left of the cut") },
        "trimEnd": { glyph: Theme.icons.trimEnd, label: qsTr("Trim end"),
                     tip: qsTr("Trim end — click a clip to drop everything right of the cut") },
        "deleteLeft": { glyph: Theme.icons.trimStart, label: qsTr("Trim start to playhead"),
                        shortcut: "deleteLeft" },
        "deleteRight": { glyph: Theme.icons.trimEnd, label: qsTr("Trim end to playhead"),
                         shortcut: "deleteRight" },
        "undo": { glyph: Theme.icons.undo, label: qsTr("Undo"), shortcut: "undo" },
        "redo": { glyph: Theme.icons.redo, label: qsTr("Redo"), shortcut: "redo" },
        "delete": { glyph: Theme.icons.trash, label: qsTr("Delete clip"), shortcut: "delete" },
        "copy": { glyph: Theme.icons.copy, label: qsTr("Copy selection"), shortcut: "copy" },
        "paste": { glyph: Theme.icons.clipboardPaste, label: qsTr("Paste at current time"),
                   shortcut: "paste" },
        "duplicate": { glyph: Theme.icons.copyPlus, label: qsTr("Duplicate clip"),
                       shortcut: "duplicate" },
        "bookmark": { glyph: Theme.icons.bookmark, label: qsTr("Add/remove bookmark at current time"),
                      shortcut: "toggleBookmark" },
        "previousBookmark": { glyph: Theme.icons.chevronLeft, label: qsTr("Go to previous marker"),
                              shortcut: "previousBookmark" },
        "nextBookmark": { glyph: Theme.icons.chevronRight, label: qsTr("Go to next marker"),
                          shortcut: "nextBookmark" },
        "markIn": { glyph: Theme.icons.setStart, label: qsTr("Mark work area in"), shortcut: "markIn" },
        "markOut": { glyph: Theme.icons.setEnd, label: qsTr("Mark work area out"), shortcut: "markOut" },
        "loop": { glyph: Theme.icons.repeat, label: qsTr("Loop work area playback"),
                  shortcut: "toggleLoop" },
        "clearWorkArea": { glyph: Theme.icons.x, label: qsTr("Clear work area"),
                           shortcut: "clearInOut" },
        "merge": { glyph: Theme.icons.linkTwo, label: qsTr("Merge adjacent clips"), shortcut: "merge" },
        // Dafin: Filmora's freeze frame holds the frame inside the selected clip; Drift's lays a
        // still on a new track over it and stays in the menu.
        "speedControl": { glyph: Theme.icons.gauge, label: qsTr("Speed control"),
                          tip: qsTr("Speed control — shows the speed bar on the selected clip; drag either end of the clip to speed it up or slow it down instead of trimming. Click again to hide it") },
        "freezeFrame": { glyph: Theme.icons.snowflake, label: qsTr("Freeze frame"),
                         tip: qsTr("Freeze frame — holds the frame at the playhead inside the selected video clip for 5 s; the rest of the clip moves later. Change the length in Speed") },
        "freeze": { glyph: Theme.icons.snowflake, label: qsTr("Freeze frame on a new track"),
                    tip: qsTr("Freeze frame on a new track — lays a still of the frame at the playhead over the timeline, leaving the clip as it is") },
        "adjustmentLayer": { glyph: Theme.icons.wand, label: qsTr("Add adjustment layer") },
        "transformLayer": { glyph: Theme.icons.group, label: qsTr("Add transform layer"),
                            tip: qsTr("Add transform layer — moves the selected clips' tracks as one"),
                            shortcut: "transformTogether" },
        "camera": { glyph: Theme.icons.video, label: qsTr("Add camera"),
                    tip: qsTr("Add camera — one viewpoint the whole timeline is seen through. With a camera already there, adds a new camera clip (a cut to a new framing) at the playhead") }
    })

    // Dafin: groups are Edit | History | Tools | Audio and links | Delete, with the destructive
    // Delete last and apart. Only the default: a stored (customised) layout is used as saved.
    readonly property var defaultToolbarItems: [
        "select", "cut", "keyframe", "snap", "speedControl", "freezeFrame", "deleteLeft", "deleteRight",
        "separator", "undo", "redo",
        "separator", "chromaKey", "motionTrack", "syncAudio", "beatMarkers", "autoDuck", "separator",
        "separateAudio",
        "unlink", "link", "separator", "delete"
    ]
    // Dafin's tools, which join a toolbar arranged before they existed as a group after Delete
    // (or, when some are already there, right after the last of them).
    readonly property var dafinTools: ["chromaKey", "motionTrack", "syncAudio", "beatMarkers", "autoDuck"]
    readonly property var defaultMenuItems: [
        "trimStart", "trimEnd", "separator", "copy", "paste", "duplicate", "separator",
        "bookmark", "previousBookmark", "nextBookmark", "markIn", "markOut", "loop",
        "clearWorkArea", "separator",
        "merge", "freeze", "adjustmentLayer", "transformLayer", "camera"
    ]

    // The stored layout, cleaned: unknown or repeated ids are dropped and any action missing
    // from both lists (one added in a later version) lands at the end of the menu.
    readonly property var layout: {
        const storedToolbar = EditorState.preferences.timelineToolbarItems
        const storedMenu = EditorState.preferences.timelineMenuItems
        if (storedToolbar.length === 0 && storedMenu.length === 0)
            return { toolbar: defaultToolbarItems, menu: defaultMenuItems }
        const seen = {}
        const clean = (list) => list.filter((id) => {
            if (id === "separator")
                return true
            if (!actionMeta[id] || seen[id])
                return false
            seen[id] = true
            return true
        })
        const toolbarItems = clean(storedToolbar)
        const menuItems = clean(storedMenu)
        // Dafin: the keyframe button joins Select and Cut mode on a toolbar arranged before it
        // existed, rather than landing at the end of the More menu.
        if (!seen["keyframe"]) {
            seen["keyframe"] = true
            let at = toolbarItems.indexOf("cut")
            if (at < 0)
                at = toolbarItems.indexOf("select")
            toolbarItems.splice(at + 1, 0, "keyframe")
        }
        // ...and so does the snapping magnet, after the keyframe button.
        if (!seen["snap"]) {
            seen["snap"] = true
            let at = toolbarItems.indexOf("keyframe")
            if (at < 0)
                at = toolbarItems.indexOf("cut")
            toolbarItems.splice(at + 1, 0, "snap")
        }
        // ...and chroma key, motion tracking, audio sync, beat markers and auto ducking: right after
        // whichever of them is already on the toolbar, else together after Delete (else at the
        // end), between separators.
        const missing = dafinTools.filter((id) => !seen[id])
        if (missing.length > 0) {
            for (const id of missing)
                seen[id] = true
            let last = -1
            for (const id of dafinTools)
                last = Math.max(last, toolbarItems.indexOf(id))
            const at = toolbarItems.indexOf("delete")
            const group = ["separator"].concat(missing)
            if (last >= 0)
                toolbarItems.splice(last + 1, 0, ...missing)
            else if (at >= 0)
                toolbarItems.splice(at + 1, 0, ...group, "separator")
            else
                toolbarItems.push(...group)
        }
        // ...and speed control and freeze frame, after the snapping magnet (else after the
        // keyframe button, Cut or Select), so a toolbar arranged before them gets them too.
        for (const id of ["speedControl", "freezeFrame"]) {
            if (seen[id])
                continue
            seen[id] = true
            let at = -1
            for (const anchor of ["freezeFrame", "speedControl", "snap", "keyframe", "cut", "select"]) {
                if (anchor === id)
                    continue
                at = toolbarItems.indexOf(anchor)
                if (at >= 0)
                    break
            }
            toolbarItems.splice(at + 1, 0, id)
        }
        for (const id of Object.keys(actionMeta)) {
            if (!seen[id])
                menuItems.push(id)
        }
        return { toolbar: toolbarItems, menu: menuItems }
    }
}
