pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin's Motion tracking button (timeline toolbar, or its shortcut). What it does depends on the
// selection (TrackingController::motionPlan):
//  - a title, sticker, Lottie or image over a video: straight to clicking the point to follow;
//  - a video, or nothing: first pick the element(s) over that video that should follow it;
//  - one element that already follows a point: correct the point, pick a new one, or stop.
// The point is clicked on the preview (MotionPickOverlay). Tracking then runs in the background over
// just the stretch each element covers, and keeps up as the element is moved, trimmed or extended.
Item {
    id: root

    width: 0
    height: 0

    function start() {
        // A second press while the preview waits for the click backs out of it.
        if (ShellBridge.motionPick) {
            ShellBridge.motionPick = null
            return
        }
        const plan = EditorState.tracking.motionPlan()
        if (plan.mode === "choose") {
            chooser.openWith(plan)
            return
        }
        if (plan.mode !== "place") {
            Toasts.info(plan.message || qsTr("Select a title, sticker or image over a video first"))
            return
        }
        if (plan.followerIds.length === 1) {
            const info = EditorState.tracking.motionInfo(plan.followerIds[0])
            if (info.following) {
                followerMenu.openFor(plan.followerIds[0], plan.label, info)
                return
            }
        }
        root.arm("start", plan)
    }

    // Waits for the click on the preview. The playhead moves onto the element first when it is
    // elsewhere, so the point is picked on a frame the element is shown on.
    function arm(mode, plan) {
        EditorState.playing = false
        if (!plan.playheadInside && plan.suggestedSeconds !== undefined)
            EditorState.playheadSeconds = plan.suggestedSeconds
        ShellBridge.motionPick = {
            mode: mode,
            hostClipId: plan.hostClipId,
            hostName: plan.hostName || "",
            followerIds: plan.followerIds,
            label: plan.label || ""
        }
    }

    Connections {
        target: EditorState
        // A pick in progress belongs to the project it was made in.
        function onProjectReset() { ShellBridge.motionPick = null }
        function onMotionTrackingRequested() { root.start() }
    }

    Connections {
        target: ShellBridge
        function onMotionTrackingRequested() { root.start() }
    }

    // Which elements over the video should follow it. Several can be ticked: each gets a tracker of
    // its own, so they can later be moved and trimmed independently.
    ThemedDialog {
        id: chooser

        property var plan: ({})
        property var picked: ({})
        readonly property int pickedCount: Object.keys(picked).filter((id) => picked[id]).length

        title: qsTr("Motion tracking")
        acceptText: qsTr("Next: click the point")
        acceptEnabled: pickedCount > 0
        preferredWidth: Theme.dialogWidthSm

        function openWith(plan) {
            chooser.plan = plan
            const picked = {}
            if (plan.candidates.length === 1)
                picked[plan.candidates[0].clipId] = true
            chooser.picked = picked
            open()
        }

        function toggle(clipId, on) {
            const next = Object.assign({}, chooser.picked)
            next[clipId] = on
            chooser.picked = next
        }

        onAccepted: {
            const chosen = chooser.plan.candidates.filter((c) => chooser.picked[c.clipId])
            if (chosen.length === 0)
                return
            // Selecting them shows which ones were picked, and lets the plan work out the frame.
            EditorState.setSelection(chosen.map((c) => ({ track: c.track, clip: c.clip })))
            const plan = EditorState.tracking.motionPlan()
            if (plan.mode !== "place") {
                Toasts.info(plan.message || qsTr("Those elements are not over a video"))
                return
            }
            root.arm("start", plan)
        }

        contentItem: Column {
            spacing: Theme.spacingLg
            width: parent ? parent.width : 360

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Which elements should follow something in “%1”? Next you click the spot in the video they follow.")
                      .arg(chooser.plan.hostName || qsTr("the video"))
                color: Theme.c.themedDialog.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Flickable {
                width: parent.width
                height: Math.min(contentHeight, Math.max(120, chooser.availableContentHeight - 80))
                contentHeight: candidateColumn.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                Column {
                    id: candidateColumn
                    width: parent.width
                    spacing: Theme.spacingXs

                    Repeater {
                        model: chooser.plan.candidates || []

                        ThemedCheckBox {
                            required property var modelData
                            width: candidateColumn.width
                            text: modelData.following ? qsTr("%1 (already follows a point)").arg(modelData.name)
                                                      : modelData.name
                            checked: !!chooser.picked[modelData.clipId]
                            onToggled: chooser.toggle(modelData.clipId, checked)
                        }
                    }
                }
            }
        }
    }

    // For an element that already follows a point.
    ThemedContextMenu {
        id: followerMenu

        property string followerId: ""
        property string label: ""
        property var info: ({})

        implicitWidth: 300

        function openFor(followerId, label, info) {
            followerMenu.followerId = followerId
            followerMenu.label = label
            followerMenu.info = info
            followerMenu.popup()
        }

        ThemedMenuItem {
            sectionHeader: true
            text: qsTr("“%1” follows a point in “%2”").arg(followerMenu.label).arg(followerMenu.info.hostName || "")
        }
        ThemedMenuItem {
            text: qsTr("Correct the point at the playhead")
            icon.name: Theme.icons.crosshair
            onTriggered: root.arm("correct", {
                hostClipId: followerMenu.info.hostClipId,
                hostName: followerMenu.info.hostName,
                followerIds: [followerMenu.followerId],
                label: followerMenu.label,
                playheadInside: true
            })
        }
        ThemedMenuItem {
            text: qsTr("Pick a new point")
            icon.name: Theme.icons.motionTrack
            onTriggered: {
                const plan = EditorState.tracking.motionPlan()
                if (plan.mode === "place")
                    root.arm("start", plan)
            }
        }
        ThemedMenuItem {
            visible: followerMenu.info.center === false
            text: qsTr("Centre it on the point")
            icon.name: Theme.icons.locateFixed
            onTriggered: EditorState.tracking.setMotionCenter(followerMenu.followerId, true)
        }
        ThemedMenuItem {
            visible: followerMenu.info.lost === true
            text: qsTr("Try tracking again")
            icon.name: Theme.icons.refresh
            onTriggered: EditorState.tracking.retrackMotion(followerMenu.followerId)
        }
        ThemedMenuSeparator { }
        ThemedMenuItem {
            text: qsTr("Stop motion tracking")
            icon.name: Theme.icons.x
            onTriggered: EditorState.tracking.stopMotionFollow(followerMenu.followerId)
        }
    }
}
