import QtQuick
import QtQuick.Window
import Drift
// What the canvas says when it has no picture to show, plus the diagnostics and recording badges.
// `compact` is the phone's version: one short line per state and no badges.
Item {
    id: root

    property bool compact: false

    // Top-left so it never covers the transport controls or the bottom-right resolution readout.
    // Only visible while the diagnostics dialog has the counters armed.
    Loader {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: Theme.spacingLg
        z: 10
        active: !root.compact && !!EditorState.playback.stats && EditorState.playback.stats.active
        sourceComponent: Component { PlaybackStatsOverlay { } }
    }

    // Voiceover recording indicator overlay
    PreviewVoiceoverBadge {
        visible: !root.compact && EditorState.isRecordingAudio
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spacingLg
        z: 11
        paused: EditorState.isAudioRecordingPaused
        seconds: EditorState.audioRecordSeconds
    }

    // The automatic preview quality has stepped down: this is not what the export will look like.
    // Below the voiceover badge, which only shows while recording.
    PreviewQualityBadge {
        visible: !root.compact && EditorState.playback.adaptivePreview
                 && EditorState.playback.adaptiveLevel > 0
        level: EditorState.playback.adaptiveLevel
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spacingLg
        anchors.topMargin: Theme.spacingLg + (EditorState.isRecordingAudio ? 36 : 0)
        z: 11
    }

    // On a brand-new project this — the largest, most central panel — said nothing at all, while
    // the timeline below it explained what to do. The terse gap message below is right when a
    // project has content and the playhead is simply over a gap; it is not an answer to "what do
    // I do first".
    EmptyState {
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.spacing3xl, 280)
        visible: !root.compact && EditorState.trackCount === 0
        glyph: Theme.icons.film
        title: qsTr("Nothing to preview yet")
        // No CTA: importing and adding tracks both live in the panels either side, and this one
        // should not compete with them.
        hint: qsTr("Import media and drag it onto the timeline below to see it here.")
    }

    readonly property bool gpuFailed: EditorState.playback.gpuCompositorStatus !== "unknown"
                                      && !EditorState.playback.gpuCompositorReady

    // A dead GPU compositor produces no frame at any playhead position, which for a long time
    // read as "No clip at the current time" and sent people hunting through their timeline. Say
    // what actually happened, and where the details are. Held back until the first probe answers,
    // so a slow driver does not flash a failure during startup.
    EmptyState {
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.spacing3xl, 280)
        visible: !root.compact && EditorState.trackCount > 0 && root.gpuFailed
        glyph: Theme.icons.warning
        title: qsTr("GPU preview unavailable")
        hint: EditorState.playback.gpuCompositorStatus === "version-too-low"
              && EditorState.playback.gpuCompositorDetail
              ? qsTr("Your graphics driver only provides %1. Drift's preview needs OpenGL 3.3.")
                    .arg(EditorState.playback.gpuCompositorDetail)
              : qsTr("Drift could not start its GPU renderer, so the preview cannot draw.")
        actionText: qsTr("Debug info")
        onActionTriggered: ShellBridge.main.openDebugInfo()
    }

    PreviewCompactGpuCaption {
        visible: root.compact && root.gpuFailed
    }

    // Fades rather than popping, so scrubbing across a gap no longer flickers this text on and
    // off. Only ever a gap message: when the compositor is down the state above explains that
    // instead.
    PreviewGapCaption {
        show: !(EditorState.playback.hasFrame
                || (!root.compact && EditorState.trackCount === 0)
                || !EditorState.playback.gpuCompositorReady)
        message: EditorState.activeAudioClipAtPlayhead().path
                 ? qsTr("Audio only") : qsTr("No clip at the current time")
    }
}
