import QtQuick
import Drift

// Right-click menu for one timeline clip. The owning clip is passed in; the loader unloads it on close.
ThemedContextMenu {
    id: clipContextMenu

    required property var owner
    signal unloadRequested()

    property bool canPasteEffects: false
    property bool canPasteAttributes: false
    property bool canMergeTrackSubtitles: false
    property bool canRemoveSilence: false
    property bool canSyncByAudio: false
    property bool canReplaceMedia: false
    onAboutToShow: {
        canRemoveSilence = EditorState.audioAlign.canRemoveSilence(owner.trackIndex, owner.clipIndex)
        canSyncByAudio = EditorState.audioAlign.canSyncByAudio()
        canReplaceMedia = typeof owner.panel.requestReplaceClip === "function"
                && EditorState.replaceCandidatesForClip(owner.trackIndex, owner.clipIndex).length > 0
        canPasteEffects = EditorState.clipboardHasEffects()
        canPasteAttributes = EditorState.canPasteAttributes()
        canMergeTrackSubtitles = owner.trackType === "subtitle"
                && EditorState.canMergeAllSubtitlesOnTrack(owner.trackIndex)
        const clip = EditorState.clipAt(owner.trackIndex, owner.clipIndex) || {}
        clipReversed = !!clip.reverse
        rebuildMoveToTrackMenu()
        rebuildSpeedMenu()
    }

    // Dafin: Filmora's Speed submenu (Show Speed Control, Add Freeze Frame, Uniform Speed, Speed
    // Ramping) and Reverse, on clips that play at a speed. A submenu cannot be hidden in place, so
    // it is built for the clip each time the menu opens, as Move to track is.
    readonly property bool retimable: owner.clipData.kind === "video" || owner.clipData.kind === "audio"
                                      || owner.clipData.kind === "composite"
    property bool clipReversed: false
    property var speedMenu: null

    function rebuildSpeedMenu() {
        if (speedMenu) {
            removeMenu(speedMenu)
            speedMenu = null
        }
        if (!retimable)
            return
        const sub = speedMenuComponent.createObject(null)
        let at = count
        for (let i = 0; i < count; i++) {
            if (itemAt(i) === reverseItem) {
                at = i
                break
            }
        }
        insertMenu(at, sub)
        speedMenu = sub
    }

    function openSpeedPage(page) {
        if (!owner.selected)
            EditorState.selectClip(owner.trackIndex, owner.clipIndex)
        ShellBridge.speedPageRequest = page
        Qt.callLater(() => ShellBridge.navigate("panel:properties.speed"))
    }

    Component {
        id: speedMenuComponent
        ThemedContextMenu {
            id: speedSub
            title: qsTr("Speed")
            readonly property var clipOwner: clipContextMenu.owner
            readonly property bool controlShown: EditorState.speedControlVisible(clipOwner.trackIndex,
                                                                                 clipOwner.clipIndex)
            readonly property bool overClip: EditorState.playheadSeconds >= (clipOwner.clipData.start || 0)
                                             && EditorState.playheadSeconds < (clipOwner.clipData.start || 0)
                                                                              + (clipOwner.clipData.duration || 0)
            ThemedMenuItem {
                text: speedSub.controlShown ? qsTr("Hide Speed Control") : qsTr("Show Speed Control")
                icon.name: Theme.icons.gauge
                onTriggered: EditorState.setSpeedControlVisible(speedSub.clipOwner.trackIndex,
                                                                speedSub.clipOwner.clipIndex,
                                                                !speedSub.controlShown)
            }
            ThemedMenuItem {
                text: qsTr("Add Freeze Frame")
                icon.name: Theme.icons.snowflake
                visible: speedSub.clipOwner.clipData.kind === "video"
                enabled: speedSub.overClip
                // Five seconds, as in Filmora; Speed › Duration changes it afterwards.
                onTriggered: EditorState.insertFreezeFrame(speedSub.clipOwner.trackIndex,
                                                           speedSub.clipOwner.clipIndex, 5.0)
            }
            ThemedMenuItem {
                text: qsTr("Uniform Speed")
                icon.name: Theme.icons.gauge
                onTriggered: clipContextMenu.openSpeedPage("uniform")
            }
            ThemedMenuItem {
                text: qsTr("Speed Ramping")
                icon.name: Theme.icons.gauge
                visible: speedSub.clipOwner.clipData.kind !== "composite"
                onTriggered: clipContextMenu.openSpeedPage("ramping")
            }
        }
    }

    property var moveToTrackMenu: null

    function rebuildMoveToTrackMenu() {
        if (moveToTrackMenu) {
            removeMenu(moveToTrackMenu)
            moveToTrackMenu = null
        }
        const targets = EditorState.compatibleTracksForClip(owner.trackIndex, owner.clipIndex)
        if (targets.length === 0)
            return
        const sub = moveToTrackMenuComponent.createObject(null)
        for (let i = 0; i < targets.length; i++) {
            const label = targets[i].name || owner.panel.trackNameAt(targets[i].index)
            sub.addItem(moveToTrackItemComponent.createObject(null,
                { text: label, targetTrack: targets[i].index }))
        }
        let at = count
        for (let i = 0; i < count; i++) {
            if (itemAt(i) === moveToOwnTrackItem) {
                at = i + 1
                break
            }
        }
        insertMenu(at, sub)
        moveToTrackMenu = sub
    }

    Component {
        id: moveToTrackMenuComponent
        ThemedContextMenu {
            title: qsTr("Move to track")
        }
    }

    Component {
        id: moveToTrackItemComponent
        ThemedMenuItem {
            required property int targetTrack
            onTriggered: EditorState.moveClipToTrack(clipContextMenu.owner.trackIndex,
                                                     clipContextMenu.owner.clipIndex, targetTrack,
                                                     clipContextMenu.owner.clipData.start || 0)
        }
    }
    // Deferred: onClosed runs inside the Popup's own close path, and dropping the
    // Loader's item there would destroy an object still unwinding.
    onClosed: unloadRequested()

    ThemedMenuItem {
        text: qsTr("Properties")
        icon.name: Theme.icons.sliders
        onTriggered: {
            if (!owner.selected)
                EditorState.selectClip(owner.trackIndex, owner.clipIndex)
            if (typeof owner.panel.openClipProperties === "function")
                owner.panel.openClipProperties()
        }
    }
    ThemedMenuItem {
        // Touch route into multi-clip selection; the panel owns the mode and
        // the desktop panel does not declare the property at all.
        text: qsTr("Select multiple")
        icon.name: Theme.icons.check
        visible: owner.panel.multiSelectActive === false
        onTriggered: owner.panel.multiSelectActive = true
    }
    ThemedMenuItem {
        text: qsTr("Open composite")
        icon.name: Theme.icons.layers
        visible: owner.clipData.kind === "composite"
        onTriggered: EditorState.openCompositeClip(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuItem {
        text: qsTr("Flatten composite")
        icon.name: Theme.icons.film
        visible: owner.clipData.kind === "composite"
        enabled: !EditorState.exportInProgress
        onTriggered: EditorState.flattenComposite(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuItem {
        text: qsTr("Dissolve composite")
        icon.name: Theme.icons.layers
        visible: owner.clipData.kind === "composite"
        enabled: EditorState.canDissolveComposite(owner.trackIndex, owner.clipIndex)
        onTriggered: EditorState.dissolveComposite(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuItem {
        text: qsTr("Make composite")
        icon.name: Theme.icons.layers
        visible: EditorState.makeCompositeAvailable
        onTriggered: EditorState.makeCompositeFromSelection()
    }
    ThemedMenuItem {
        text: EditorState.selection.length > 1 ? qsTr("Transform together")
                                               : qsTr("Add transform layer")
        icon.name: Theme.icons.group
        visible: EditorState.transformTogetherAvailable && !owner.isTransformClip
        onTriggered: EditorState.makeTransformLayerFromSelection()
    }
    ThemedMenuItem {
        text: qsTr("Select transform layer")
        icon.name: Theme.icons.maximize
        visible: owner.hasTransformParent && !owner.isTransformClip
        onTriggered: {
            EditorState.selectClip(owner.trackIndex, owner.clipIndex)
            EditorState.selectTransformParent()
        }
    }
    ThemedMenuItem {
        text: qsTr("Covers…")
        icon.name: Theme.icons.layers
        visible: owner.isTransformClip
        onTriggered: {
            if (!owner.selected)
                EditorState.selectClip(owner.trackIndex, owner.clipIndex)
            if (typeof owner.panel.openClipProperties === "function")
                owner.panel.openClipProperties()
        }
    }
    ThemedMenuItem {
        text: qsTr("Select covered clips")
        icon.name: Theme.icons.maximize
        visible: owner.isTransformClip
        onTriggered: EditorState.selectTransformChildren(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuSeparator { }
    ThemedMenuItem {
        text: qsTr("Split at current time")
        icon.name: Theme.icons.scissors
        onTriggered: EditorState.splitAtPlayhead()
    }
    ThemedMenuItem {
        text: qsTr("Split item at current time")
        icon.name: Theme.icons.scissors
        // Scoped to just this clip (and its linked partner, e.g. companion
        // audio) — splitAtPlayhead() above cuts every clip under the playhead
        // across every track, which is surprising when picked from one clip's menu.
        visible: EditorState.playheadSeconds > owner.clipData.start
                && EditorState.playheadSeconds < owner.clipData.start + owner.clipData.duration
        onTriggered: EditorState.splitClipAt(owner.trackIndex, owner.clipIndex,
                                             EditorState.playheadSeconds)
    }
    ThemedMenuItem {
        text: qsTr("Separate audio")
        icon.name: Theme.icons.audioLines
        // CapCut: only offer extract when the clip still has embedded audio.
        visible: owner.trackType === "video"
                 && EditorState.canSeparateAudioClip(owner.trackIndex, owner.clipIndex)
        onTriggered: EditorState.separateAudioFromClip(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuItem {
        text: qsTr("Separate all audio tracks")
        icon.name: Theme.icons.audioLines
        visible: owner.trackType === "video"
                 && EditorState.canSeparateAudioClip(owner.trackIndex, owner.clipIndex)
                 && EditorState.clipAudioStreamCount(owner.trackIndex, owner.clipIndex) > 1
        onTriggered: EditorState.separateAllAudioTracks(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuItem {
        text: qsTr("Remove silence")
        icon.name: Theme.icons.audioLines
        visible: clipContextMenu.canRemoveSilence
        enabled: !EditorState.audioAlign.running
        onTriggered: EditorState.audioAlign.removeSilence(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuItem {
        text: qsTr("Sync by audio")
        icon.name: Theme.icons.audioLines
        visible: clipContextMenu.canSyncByAudio
        enabled: !EditorState.audioAlign.running
        onTriggered: EditorState.audioAlign.syncByAudio()
    }
    ThemedMenuItem {
        text: qsTr("Cancel audio analysis")
        icon.name: Theme.icons.x
        visible: EditorState.audioAlign.running
        onTriggered: EditorState.audioAlign.cancel()
    }
    ThemedMenuItem {
        readonly property string blocker: EditorState.masking.convertClipToMaskBlocker(owner.trackIndex, owner.clipIndex)
        text: blocker !== "" ? qsTr("Convert to mask") + " — " + blocker : qsTr("Convert to mask")
        icon.name: Theme.icons.mask
        visible: owner.trackType === "video"
        enabled: EditorState.masking.canConvertClipToMask(owner.trackIndex, owner.clipIndex)
        onTriggered: EditorState.masking.convertClipToMask(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuItem {
        text: qsTr("Convert to edit-friendly format")
        icon.name: Theme.icons.rabbit
        // Converts the bin media, so every clip using it follows along.
        visible: owner.mediaAssetId.length > 0 && !owner.mediaEditFriendly
        onTriggered: EditorState.bin.convertToConstantFrameRate([owner.mediaAssetId])
    }
    ThemedMenuItem {
        text: qsTr("Crop…")
        icon.name: Theme.icons.crop
        // Desktop only: AndroidMain has no crop window for timeline clips.
        visible: (owner.clipData.kind === "video" || owner.clipData.kind === "image")
                 && typeof ShellBridge.main?.openCrop === "function"
        onTriggered: ShellBridge.main.openCrop(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuItem {
        text: qsTr("Enhance video…")
        icon.name: Theme.icons.sparkles
        // Desktop only: AndroidMain has no restore window.
        visible: owner.clipData.kind === "video"
                 && typeof ShellBridge.main?.openRestore === "function"
        onTriggered: ShellBridge.main.openRestore(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuItem {
        id: reverseItem
        // Dafin: as in Filmora's menu, under Speed. Turning reverse on may need a reversed copy
        // rendered first, which requestClipReverse confirms (ReverseProgressDialog).
        text: clipContextMenu.clipReversed ? qsTr("Play forward") : qsTr("Reverse")
        icon.name: Theme.icons.reset
        visible: owner.clipData.kind === "video" || owner.clipData.kind === "audio"
        onTriggered: {
            if (clipContextMenu.clipReversed)
                EditorState.setClipReverse(owner.trackIndex, owner.clipIndex, false)
            else
                EditorState.requestClipReverse(owner.trackIndex, owner.clipIndex)
        }
    }
    ThemedMenuItem {
        text: qsTr("Unlink")
        icon.name: Theme.icons.unlink
        visible: !!owner.clipData.linked && EditorState.unlinkAvailable
        onTriggered: EditorState.unlinkSelectedClips()
    }
    ThemedMenuItem {
        text: qsTr("Link")
        icon.name: Theme.icons.link
        // Needs two or more unlinked clips selected (Shift+click to add).
        visible: EditorState.linkAvailable
        onTriggered: EditorState.linkSelectedClips()
    }
    ThemedMenuItem {
        text: qsTr("Merge subtitle clips")
        icon.name: Theme.icons.linkTwo
        visible: owner.trackType === "subtitle"
        enabled: EditorState.mergeAvailable
        onTriggered: EditorState.mergeSelectedClips()
    }
    ThemedMenuItem {
        text: qsTr("Merge all subtitles on this track")
        icon.name: Theme.icons.linkTwo
        visible: owner.trackType === "subtitle"
        enabled: clipContextMenu.canMergeTrackSubtitles
        onTriggered: EditorState.mergeAllSubtitlesOnTrack(owner.trackIndex)
    }
    ThemedMenuItem {
        text: qsTr("Convert to text clips")
        icon.name: Theme.icons.type
        visible: owner.trackType === "subtitle"
        onTriggered: EditorState.convertSubtitleToTextClips(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuItem {
        text: qsTr("Convert to subtitle")
        icon.name: Theme.icons.captions
        visible: owner.trackType === "text" && EditorState.textToSubtitleAvailable
        onTriggered: owner.panel.requestConvertTextToSubtitle()
    }
    ThemedMenuSeparator { }
    ThemedMenuItem {
        text: qsTr("Cut")
        icon.name: Theme.icons.scissors
        onTriggered: EditorState.cutSelection()
    }
    ThemedMenuItem {
        text: qsTr("Copy")
        icon.name: Theme.icons.copy
        onTriggered: EditorState.copySelection()
    }
    ThemedMenuItem {
        text: qsTr("Paste attributes…")
        icon.name: Theme.icons.clipboardPaste
        enabled: clipContextMenu.canPasteAttributes
        onTriggered: EditorState.requestPasteAttributes()
    }
    ThemedMenuItem {
        text: qsTr("Duplicate")
        icon.name: Theme.icons.copyPlus
        onTriggered: EditorState.duplicateSelectedClip()
    }
    ThemedMenuItem {
        text: qsTr("Rename…")
        icon.name: Theme.icons.pencil
        onTriggered: owner.panel.requestRenameClip(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuItem {
        text: qsTr("Replace with…")
        icon.name: Theme.icons.repeat
        visible: clipContextMenu.canReplaceMedia
        onTriggered: owner.panel.requestReplaceClip(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuSeparator { visible: !owner.isTransformClip }
    ThemedMenuItem {
        text: qsTr("Copy effects")
        icon.name: Theme.icons.wand
        visible: owner.hasAnyEffects
        onTriggered: EditorState.copyClipEffectsToClipboard(owner.trackIndex,
                                                            owner.clipIndex)
    }
    ThemedMenuItem {
        text: qsTr("Paste effects")
        icon.name: Theme.icons.clipboardPaste
        visible: !owner.isTransformClip
        enabled: clipContextMenu.canPasteEffects
        onTriggered: EditorState.pasteEffectsFromClipboard(owner.trackIndex,
                                                           owner.clipIndex)
    }
    ThemedMenuItem {
        text: qsTr("Save effects as preset…")
        icon.name: Theme.icons.save
        visible: owner.hasAnyEffects
        onTriggered: owner.panel.requestSaveEffectPreset(owner.trackIndex,
                                                             owner.clipIndex)
    }
    ThemedMenuItem {
        text: qsTr("Color grade…")
        icon.name: Theme.icons.blend
        visible: owner.clipData.kind !== "audio" && owner.clipData.kind !== "subtitle"
        onTriggered: {
            if (!owner.selected)
                EditorState.selectClip(owner.trackIndex, owner.clipIndex)
            Workspaces.activeId = "color"
        }
    }
    ThemedMenuItem {
        text: qsTr("Blur area…")
        icon.name: Theme.icons.eyeOff
        visible: owner.trackType === "video" && !owner.isTransformClip
        onTriggered: EditorState.addEffect(owner.trackIndex, owner.clipIndex,
                                           "builtin.effects.privacy_blur")
    }
    ThemedMenuItem {
        text: qsTr("Edit keyframes…")
        icon.name: Theme.icons.diamondPlus
        onTriggered: {
            if (!owner.selected)
                EditorState.selectClip(owner.trackIndex, owner.clipIndex)
            Workspaces.activeId = "keyframes"
        }
    }
    ThemedMenuItem {
        text: qsTr("Mask & track…")
        icon.name: Theme.icons.mask
        visible: owner.trackType === "video"
        onTriggered: {
            if (!owner.selected)
                EditorState.selectClip(owner.trackIndex, owner.clipIndex)
            Workspaces.activeId = "masking-tracking"
        }
    }
    ThemedMenuSeparator { visible: owner.clipData.kind === "adjustment" && !owner.isTransformClip }
    ThemedMenuItem {
        text: qsTr("Unlink from clip")
        icon.name: Theme.icons.unlink
        visible: owner.pinnedToClip
        onTriggered: EditorState.unlinkAdjustment(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuItem {
        id: moveToOwnTrackItem
        text: qsTr("Move to its own track")
        icon.name: Theme.icons.layers
        // Only meaningful for a nested one: a standalone adjustment already has one.
        visible: owner.clipData.kind === "adjustment"
                 && owner.panel.tracks[owner.trackIndex].isAdjustmentLane === true
        onTriggered: EditorState.moveAdjustmentToOwnTrack(owner.trackIndex,
                                                          owner.clipIndex)
    }
    ThemedMenuSeparator { }
    ThemedMenuItem {
        text: qsTr("Delete")
        icon.name: Theme.icons.trash
        onTriggered: EditorState.deleteSelectedClip()
    }
}
