import QtQuick
import QtQuick.Shapes
import QtMultimedia
import Drift

// One result in an online asset grid (Filmora-style card): a large preview, the name, a line of
// details, and a round download button in the preview's bottom-right corner.
//
// Previews by kind:
//   video          thumbnail; hovering plays the provider's small preview clip, muted
//   music / sfx    waveform image, cover art or waveform peaks; ▶ plays the preview (never on hover)
//   sticker/title  the Lottie itself, drawn by Dafin's renderer (image://lottieframe/), animating
//                  on hover and resting on a representative frame
// Download button states: none → queued → downloading (ring) → done (✓, click adds it again) /
// failed (retry). Items whose licence forbids download show a lock with the reason.
Item {
    id: root

    required property var item           // AssetItem map from AssetSearchModel
    required property string thumbnail   // image://assetthumb/... or ""
    property real cardWidth: 160

    signal added()

    readonly property string key: item.key || ""
    readonly property string kind: item.kind || "video"
    readonly property bool isAudio: kind === "music" || kind === "sfx"
    readonly property bool isLottie: kind === "sticker" || kind === "title"
    readonly property real aspect: kind === "sticker" ? 1.0 : isAudio ? 0.42 : 9 / 16
    readonly property string dlState: OnlineAssets.downloadsRevision >= 0 ? OnlineAssets.downloadState(key) : "none"
    readonly property real dlProgress: OnlineAssets.downloadsRevision >= 0 ? OnlineAssets.downloadProgress(key) : 0
    readonly property string dlMessage: OnlineAssets.downloadsRevision >= 0 ? OnlineAssets.downloadMessage(key) : ""
    readonly property bool hovered: hover.hovered
    property string lottiePath: ""
    property bool lottieFailed: false
    property bool playing: false
    property var peaks: (item.extra && item.extra.peaks) ? item.extra.peaks : []

    width: cardWidth
    height: preview.height + caption.height + Theme.spacingXs

    function formatDuration(s) {
        s = Math.round(s || 0)
        return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
    }
    function resolutionLabel() {
        const h = Math.min(item.width || 0, item.height || 0)
        if (h >= 2160) return "4K"
        if (h >= 1440) return "2K"
        if (h >= 1080) return "1080p"
        if (h >= 720) return "720p"
        return h > 0 ? h + "p" : ""
    }
    function startDownload() {
        if (!item.downloadable || dlState === "downloading" || dlState === "queued")
            return
        OnlineAssets.download(item)
    }

    Rectangle {
        id: preview
        width: root.width
        height: Math.round(root.width * root.aspect)
        radius: Theme.radiusSm
        clip: true
        color: Theme.c.assetsPanel.pickerBackground
        border.width: root.hovered ? 2 : 1
        border.color: root.hovered ? Theme.primary : Theme.panelBorder

        // ── still image: video thumbnail, audio waveform/cover ──
        Image {
            id: thumb
            anchors.fill: parent
            anchors.margins: 1
            source: root.isLottie ? "" : root.thumbnail
            asynchronous: true
            fillMode: root.isAudio ? Image.PreserveAspectFit : Image.PreserveAspectCrop
            sourceSize: Qt.size(Math.round(width * Screen.devicePixelRatio), Math.round(height * Screen.devicePixelRatio))
            visible: status === Image.Ready && !(video.visible)
            opacity: root.isAudio ? 0.9 : 1.0
        }

        // ── video hover preview ──
        MediaPlayer {
            id: player
            source: root.kind === "video" && root.hovered ? (root.item.previewUrl || "") : ""
            videoOutput: video
            loops: MediaPlayer.Infinite
            audioOutput: AudioOutput { muted: true }
            onSourceChanged: if (source.toString().length > 0) play()
        }
        VideoOutput {
            id: video
            anchors.fill: parent
            anchors.margins: 1
            fillMode: VideoOutput.PreserveAspectCrop
            visible: root.kind === "video" && player.playbackState === MediaPlayer.PlayingState
        }

        // ── audio: waveform peaks when there is no image ──
        Shape {
            anchors.fill: parent
            anchors.margins: 8
            visible: root.isAudio && thumb.status !== Image.Ready && root.peaks.length > 0
            ShapePath {
                strokeWidth: 0
                fillColor: Theme.primary
                PathPolyline {
                    path: {
                        const p = root.peaks
                        const n = Math.min(p.length, 160)
                        if (n === 0) return []
                        const w = preview.width - 16, h = preview.height - 16, mid = h / 2
                        const pts = []
                        for (let i = 0; i < n; ++i) {
                            const v = Math.abs(Number(p[Math.floor(i * p.length / n)])) || 0
                            pts.push(Qt.point(i * w / (n - 1), mid - v * mid))
                        }
                        for (let i = n - 1; i >= 0; --i) {
                            const v = Math.abs(Number(p[Math.floor(i * p.length / n)])) || 0
                            pts.push(Qt.point(i * w / (n - 1), mid + v * mid))
                        }
                        return pts
                    }
                }
            }
        }
        MediaPlayer {
            id: audio
            source: root.isAudio && root.playing ? (root.item.previewUrl || root.item.downloadUrl || "") : ""
            audioOutput: AudioOutput {}
            onSourceChanged: if (source.toString().length > 0) play()
            onPlaybackStateChanged: if (playbackState === MediaPlayer.StoppedState) root.playing = false
        }
        IconButton {
            visible: root.isAudio
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 6
            glyph: root.playing ? Theme.icons.pause : Theme.icons.play
            variant: "ghost"
            tooltip: root.playing ? qsTr("Stop") : qsTr("Listen")
            onClicked: root.playing = !root.playing
        }

        // ── Lottie: frames from Dafin's renderer; two images take turns so it never blinks ──
        property real lottieT: root.item.extra && root.item.extra.previewAt !== undefined ? root.item.extra.previewAt : 0.5
        property bool frontIsA: true
        function frameUrl(t) {
            return "image://lottieframe/" + encodeURIComponent(root.lottiePath) + "?t=" + t.toFixed(3)
        }
        Image {
            id: frameA
            anchors.fill: parent
            anchors.margins: root.kind === "sticker" ? 10 : 2
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            cache: false
            sourceSize: Qt.size(Math.round(width * Screen.devicePixelRatio), Math.round(height * Screen.devicePixelRatio))
            visible: root.isLottie && preview.frontIsA && status === Image.Ready
            onStatusChanged: if (status === Image.Ready && !preview.frontIsA) preview.frontIsA = true
        }
        Image {
            id: frameB
            anchors.fill: frameA
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            cache: false
            sourceSize: frameA.sourceSize
            visible: root.isLottie && !preview.frontIsA && status === Image.Ready
            onStatusChanged: if (status === Image.Ready && preview.frontIsA) preview.frontIsA = false
        }
        Timer {
            interval: 66
            repeat: true
            running: root.isLottie && root.hovered && root.lottiePath.length > 0
            onTriggered: {
                const back = preview.frontIsA ? frameB : frameA
                if (back.status === Image.Loading)
                    return
                preview.lottieT = (preview.lottieT + 0.025) % 1.0
                back.source = preview.frameUrl(preview.lottieT)
            }
        }

        // ── placeholders ──
        IconGlyph {
            anchors.centerIn: parent
            visible: !root.isAudio && !video.visible && thumb.status !== Image.Ready
                     && !(root.isLottie && (frameA.status === Image.Ready || frameB.status === Image.Ready))
            glyph: root.kind === "video" ? Theme.icons.film : Theme.icons.smile
            iconSize: 26
            iconColor: Theme.mutedForeground
            opacity: root.isLottie && !root.lottieFailed && root.lottiePath.length === 0 ? 0.4 : 0.8
        }
        IconGlyph {
            anchors.centerIn: parent
            visible: root.isAudio && thumb.status !== Image.Ready && root.peaks.length === 0
            glyph: Theme.icons.music
            iconSize: 24
            iconColor: Theme.mutedForeground
        }

        // ── badges: resolution + duration (top-left / bottom-left) ──
        Row {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: 5
            spacing: 4
            Repeater {
                model: {
                    const out = []
                    if (root.kind === "video" && root.resolutionLabel().length > 0)
                        out.push(root.resolutionLabel())
                    if ((root.item.duration || 0) > 0 && root.kind !== "sticker")
                        out.push(root.formatDuration(root.item.duration))
                    return out
                }
                delegate: Rectangle {
                    required property string modelData
                    radius: 3
                    color: "#b0000000"
                    width: badge.implicitWidth + 8
                    height: badge.implicitHeight + 2
                    Text {
                        id: badge
                        anchors.centerIn: parent
                        text: parent.modelData
                        color: "white"
                        font.pixelSize: Theme.fontSizeXs
                    }
                }
            }
        }

        // ── download button (bottom-right) ──
        Rectangle {
            id: dl
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 5
            width: 28
            height: 28
            radius: 14
            visible: root.hovered || root.dlState !== "none" || !root.item.downloadable
            color: root.dlState === "done" ? Theme.primary
                 : root.dlState === "failed" ? "#c0392b"
                 : "#cc000000"
            border.color: dlHover.hovered ? "white" : "transparent"
            border.width: 1

            IconGlyph {
                anchors.centerIn: parent
                visible: root.dlState !== "downloading" && root.dlState !== "queued"
                glyph: !root.item.downloadable ? Theme.icons.lock
                     : root.dlState === "done" ? Theme.icons.check
                     : root.dlState === "failed" ? Theme.icons.refresh
                     : Theme.icons.download
                iconSize: 15
                iconColor: root.dlState === "done" ? Theme.primaryForeground : "white"
            }
            CircularProgress {
                anchors.centerIn: parent
                visible: root.dlState === "downloading" || root.dlState === "queued"
                size: 22
                value: root.dlProgress
                indeterminate: root.dlState === "queued" || root.dlProgress <= 0
            }
            HoverHandler { id: dlHover; cursorShape: root.item.downloadable ? Qt.PointingHandCursor : Qt.ArrowCursor }
            TapHandler {
                onTapped: {
                    if (root.dlState === "downloading" || root.dlState === "queued")
                        OnlineAssets.cancelDownload(root.key)
                    else
                        root.startDownload()
                }
            }
            ThemedToolTip {
                visible: dlHover.hovered
                text: !root.item.downloadable ? (root.item.downloadBlockedReason || qsTr("Download not allowed"))
                    : root.dlState === "done" ? qsTr("Downloaded. Click to add it to Media again.")
                    : root.dlState === "downloading" ? qsTr("Downloading %1%. Click to cancel.").arg(Math.round(root.dlProgress * 100))
                    : root.dlState === "queued" ? qsTr("Waiting… Click to cancel.")
                    : root.dlState === "failed" ? (root.dlMessage || qsTr("Download failed. Click to retry."))
                    : qsTr("Download and add to Media")
            }
        }
    }

    Column {
        id: caption
        anchors.top: preview.bottom
        anchors.topMargin: Theme.spacingXs
        width: root.width
        spacing: 1
        Text {
            width: parent.width
            elide: Text.ElideRight
            text: root.item.title || ""
            color: Theme.panelForeground
            font.pixelSize: Theme.fontSizeSm
        }
        Text {
            width: parent.width
            elide: Text.ElideRight
            visible: text.length > 0
            text: {
                const parts = []
                if (root.item.author) parts.push(root.item.author)
                if (root.isAudio && root.item.license) parts.push(root.item.license)
                return parts.join(" · ")
            }
            color: Theme.mutedForeground
            font.pixelSize: Theme.fontSizeXs
        }
    }

    HoverHandler { id: hover }
    TapHandler { onDoubleTapped: root.startDownload() }

    ThemedToolTip {
        visible: hover.hovered && !dlHover.hovered && (root.item.attribution || "").length > 0
        delay: 900
        text: root.item.attribution + (root.item.license ? "\n" + root.item.license : "")
    }

    // Lazy work: only cards the grid actually creates fetch previews / details.
    Component.onCompleted: {
        if (isLottie)
            OnlineAssets.preparePreview(item)
        else if (isAudio && !item.thumbnailUrl && peaks.length === 0 && item.providerId === "openverse")
            OnlineAssets.fetchDetails(item)
    }

    Connections {
        target: OnlineAssets
        function onPreviewReady(key, path) {
            if (key !== root.key)
                return
            root.lottiePath = path
            preview.frontIsA = true
            frameA.source = preview.frameUrl(preview.lottieT)
        }
        function onDetailsReady(detailed) {
            if (detailed.key === root.key && detailed.extra && detailed.extra.peaks)
                root.peaks = detailed.extra.peaks
        }
        function onDownloaded(key, binAssetId) {
            if (key === root.key)
                root.added()
        }
    }
}
