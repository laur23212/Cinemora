import QtQuick
import QtMultimedia
import Drift

// One result in the Stock Media tab. Hovering previews it the way it will look:
//   video  → the source's small preview clip, muted, looping
//   lottie → the animation itself, drawn by Drift's renderer (image://lottieframe/)
//   audio  → a play button (sound never starts on hover)
// The download button fetches the full file into the session folder and adds it to the media
// bin; when it is already downloaded the button adds it again.
Item {
    id: root

    required property var item          // FreeLibrary item map
    property real cardWidth: 150

    signal added()

    readonly property string key: item.key || ""
    readonly property string kind: item.kind || "video"
    readonly property string dlState: FreeLibrary.revision >= 0 ? FreeLibrary.state(key) : "none"
    readonly property real dlProgress: FreeLibrary.revision >= 0 ? FreeLibrary.progress(key) : 0
    readonly property bool hovered: hover.hovered
    property string lottiePath: ""
    property bool audioPlaying: false

    width: cardWidth
    height: preview.height + label.implicitHeight + Theme.spacingXs * 2

    Rectangle {
        id: preview
        width: root.width
        height: Math.round(root.width * 9 / 16)
        radius: Theme.radiusSm
        clip: true
        color: Theme.c.assetsPanel.pickerBackground
        border.width: root.hovered ? 2 : 1
        border.color: root.hovered ? Theme.primary : Theme.panelBorder

        Image {
            id: thumb
            anchors.fill: parent
            anchors.margins: 1
            source: root.item.thumb || ""
            fillMode: root.kind === "audio" ? Image.PreserveAspectFit : Image.PreserveAspectCrop
            asynchronous: true
            visible: status === Image.Ready && !(root.kind === "video" && player.playbackState === MediaPlayer.PlayingState)
        }

        IconGlyph {
            anchors.centerIn: parent
            visible: thumb.status !== Image.Ready && root.kind !== "lottie"
            glyph: root.kind === "audio" ? Theme.icons.music : Theme.icons.film
            iconSize: 28
            iconColor: Theme.mutedForeground
        }

        // ── video hover preview ──
        MediaPlayer {
            id: player
            source: root.kind === "video" && root.hovered ? (root.item.preview || root.item.download || "") : ""
            videoOutput: videoOut
            loops: MediaPlayer.Infinite
            audioOutput: AudioOutput { muted: true }
            onSourceChanged: if (source.toString().length > 0) play()
        }
        VideoOutput {
            id: videoOut
            anchors.fill: parent
            anchors.margins: 1
            fillMode: VideoOutput.PreserveAspectCrop
            visible: root.kind === "video" && player.playbackState === MediaPlayer.PlayingState
        }

        // ── audio preview ──
        MediaPlayer {
            id: audioPlayer
            source: root.kind === "audio" && root.audioPlaying ? (root.item.preview || root.item.download || "") : ""
            audioOutput: AudioOutput {}
            onSourceChanged: if (source.toString().length > 0) play()
            onPlaybackStateChanged: if (playbackState === MediaPlayer.StoppedState) root.audioPlaying = false
        }
        IconButton {
            visible: root.kind === "audio"
            anchors.centerIn: parent
            glyph: root.audioPlaying ? Theme.icons.pause : Theme.icons.play
            variant: "ghost"
            tooltip: root.audioPlaying ? qsTr("Stop") : qsTr("Listen")
            onClicked: root.audioPlaying = !root.audioPlaying
        }

        // ── Lottie preview: frames from Drift's renderer. Two images take turns: the next frame
        // loads into the hidden one and only becomes the shown one once it is ready, so the
        // animation never blinks to empty between frames.
        property real lottieT: 0.35
        property bool frontIsA: true
        function frameUrl(t) {
            return "image://lottieframe/" + encodeURIComponent(root.lottiePath) + "?t=" + t.toFixed(3)
        }
        Image {
            id: frameA
            anchors.fill: parent
            anchors.margins: 4
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            cache: false
            sourceSize: Qt.size(Math.round(width * Screen.devicePixelRatio), Math.round(height * Screen.devicePixelRatio))
            visible: root.kind === "lottie" && preview.frontIsA && status === Image.Ready
            onStatusChanged: if (status === Image.Ready && !preview.frontIsA) preview.frontIsA = true
        }
        Image {
            id: frameB
            anchors.fill: parent
            anchors.margins: 4
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            cache: false
            sourceSize: frameA.sourceSize
            visible: root.kind === "lottie" && !preview.frontIsA && status === Image.Ready
            onStatusChanged: if (status === Image.Ready && preview.frontIsA) preview.frontIsA = false
        }
        Timer {
            interval: 66 // ~15 fps is plenty for a thumbnail
            repeat: true
            running: root.kind === "lottie" && root.hovered && root.lottiePath.length > 0
            onTriggered: {
                // Skip a tick while the back image is still rendering the previous request.
                const back = preview.frontIsA ? frameB : frameA
                if (back.status === Image.Loading)
                    return
                preview.lottieT = (preview.lottieT + 0.03) % 1.0
                back.source = preview.frameUrl(preview.lottieT)
            }
        }
        onVisibleChanged: if (visible) root.ensureLottieStill()

        // Duration badge
        Rectangle {
            visible: (root.item.duration || 0) > 0
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: 4
            radius: 3
            color: "#b0000000"
            width: durText.implicitWidth + 8
            height: durText.implicitHeight + 2
            Text {
                id: durText
                anchors.centerIn: parent
                color: Theme.onMedia
                font.pixelSize: Theme.fontSizeXs
                text: {
                    const s = Math.round(root.item.duration || 0)
                    return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
                }
            }
        }

        // Download / add button (Filmora puts it in the corner of the thumbnail)
        Rectangle {
            id: dlButton
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 4
            width: 26
            height: 26
            radius: 13
            color: root.dlState === "ready" ? Theme.primary : "#cc000000"
            visible: root.hovered || root.dlState === "downloading" || root.dlState === "ready" || root.dlState === "failed"

            IconGlyph {
                anchors.centerIn: parent
                visible: root.dlState !== "downloading"
                glyph: root.dlState === "ready" ? Theme.icons.plus
                     : root.dlState === "failed" ? Theme.icons.refresh
                     : Theme.icons.download
                iconSize: 15
                iconColor: root.dlState === "ready" ? Theme.primaryForeground : Theme.onMedia
            }
            CircularProgress {
                anchors.centerIn: parent
                visible: root.dlState === "downloading"
                size: 22
                value: root.dlProgress
                indeterminate: root.dlProgress <= 0
            }
            TapHandler {
                onTapped: root.startDownload()
            }
            HoverHandler { cursorShape: Qt.PointingHandCursor }
            ThemedToolTip {
                visible: parent.visible && dlHover.hovered
                text: root.dlState === "ready" ? qsTr("Add to media again")
                    : root.dlState === "downloading" ? qsTr("Downloading…")
                    : qsTr("Download and add to media")
            }
            HoverHandler { id: dlHover }
        }
    }

    Text {
        id: label
        anchors.top: preview.bottom
        anchors.topMargin: Theme.spacingXs
        width: root.width
        elide: Text.ElideRight
        maximumLineCount: 1
        text: root.item.title || ""
        color: Theme.panelForeground
        font.pixelSize: Theme.fontSizeSm
    }

    HoverHandler { id: hover }
    TapHandler {
        // Double-click the card = download, like Filmora's double-click-to-apply.
        onDoubleTapped: root.startDownload()
    }

    onHoveredChanged: {
        if (hovered && kind === "lottie")
            FreeLibrary.preparePreview(item)
    }

    function ensureLottieStill() {
        if (kind === "lottie" && lottiePath.length === 0)
            FreeLibrary.preparePreview(item)
    }

    function startDownload() {
        if (dlState === "downloading")
            return
        FreeLibrary.download(item)
    }

    Connections {
        target: FreeLibrary
        function onPreviewReady(key, path) {
            if (key !== root.key)
                return
            root.lottiePath = path
            preview.frontIsA = true
            frameA.source = preview.frameUrl(0.35)
        }
        function onDownloaded(key, binAssetId) {
            if (key === root.key)
                root.added()
        }
    }

    Component.onCompleted: ensureLottieStill()
}
