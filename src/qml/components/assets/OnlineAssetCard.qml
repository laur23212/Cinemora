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
// Download button states: none → queued → downloading (ring) → done (✓ "ready": the card drags
// onto the timeline, a click adds it to Media again) / failed (retry). Items whose licence forbids download show a lock with the reason.
//
// Dafin: the user's own files (My Lotties, My sounds, imported .mogrt titles) carry Save and
// Delete in the top-right corner; an unsaved one is marked and goes away when Dafin closes. A
// .mogrt shows its poster, plays its preview on hover, says how well MOGRT Converter renders it, and
// is added (click or drag) as an editable template title; MogrtTitleDialog only when it can't be.
Item {
    id: root

    required property var item           // AssetItem map from AssetSearchModel
    required property string thumbnail   // image://assetthumb/... or ""
    property real cardWidth: 160

    signal added()
    // A saved file of the user's own: the panel confirms before deleting it.
    signal deleteRequested(var item)
    // Dafin: one preview at a time. The grid passes the key of the card that last started
    // playing; any other card that is playing stops.
    property string playingKey: ""
    signal previewStarted(string key)
    onPlayingChanged: if (playing) previewStarted(key)
    onPlayingKeyChanged: if (playing && playingKey !== key) playing = false

    readonly property string key: item.key || ""
    readonly property string kind: item.kind || "video"
    readonly property bool isAudio: kind === "music" || kind === "sfx"
    readonly property bool userItem: !!(item.extra && item.extra.userLibrary)
    readonly property bool saved: userItem && !!item.extra.saved
    readonly property bool isMogrt: userItem && item.extra.format === "mogrt"
    readonly property bool isLottie: (kind === "sticker" || kind === "title") && !isMogrt
    // Dafin: a template's file, and what MOGRT Converter makes of it (MogrtTitleController.templateStatus).
    readonly property string mogrtPath: isMogrt && item.downloadUrl ? MogrtRenderer.pathFor(item.downloadUrl) : ""
    readonly property var mogrtStatus: isMogrt && mogrtPath.length > 0
                                       ? (EditorState.mogrt.revision, EditorState.mogrt.templateStatus(item.id || "", mogrtPath))
                                       : ({})
    readonly property bool mogrtAnimates: ["checking", "ok", "warnings"].indexOf(mogrtStatus.state || "") >= 0
    readonly property real aspect: kind === "sticker" ? 1.0 : isAudio ? 0.42 : 9 / 16
    readonly property string dlState: OnlineAssets.downloadsRevision >= 0 ? OnlineAssets.downloadState(key) : "none"
    readonly property real dlProgress: OnlineAssets.downloadsRevision >= 0 ? OnlineAssets.downloadProgress(key) : 0
    readonly property string dlMessage: OnlineAssets.downloadsRevision >= 0 ? OnlineAssets.downloadMessage(key) : ""
    // Dafin: the downloaded file while it is on disk and in Media (OnlineAssets' one downloaded
    // state, shared by every tab). Non-empty = "ready": the card drags this file to the timeline.
    readonly property string readyPath: dlState === "done" && !isMogrt ? OnlineAssets.downloadedPath(key) : ""
    readonly property bool hovered: hover.hovered
    property string lottiePath: ""
    property bool lottieFailed: false
    property bool playing: false
    property var peaks: (item.extra && item.extra.peaks) ? item.extra.peaks : []
    // Dafin: audio that is already a file here (the built-in sound effects, My sounds) drags
    // straight onto the timeline as that file.
    readonly property string localAudioUrl: isAudio && item.downloadUrl && String(item.downloadUrl).startsWith("file:")
                                            ? String(item.downloadUrl) : ""

    width: cardWidth
    height: preview.height + caption.height + Theme.spacingXs

    function formatDuration(s) {
        // Sound effects are mostly under a second or two: "0.8 s" says more than "0:01".
        if ((s || 0) > 0 && s < 10)
            return qsTr("%1 s").arg(Number(s).toLocaleString(Qt.locale(), "f", 1))
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
        if (root.isMogrt) {
            root.addTitle()
            return
        }
        if (!item.downloadable || dlState === "downloading" || dlState === "queued")
            return
        OnlineAssets.download(item)
    }
    // A Motion Graphics Template: added as an editable template title, animated by MOGRT Converter.
    // When it can't be animated here the dialog says why and offers an ordinary Dafin title.
    function addTitle() {
        if (!root.mogrtAnimates && ShellBridge.main && ShellBridge.main.openMogrtTitleDialog) {
            ShellBridge.main.openMogrtTitleDialog(root.item)
            return
        }
        const result = EditorState.mogrt.addTemplate(root.mogrtPath, -1, -1, root.item.title || "")
        if (result && result.ok)
            root.added()
    }
    function deleteItem() {
        if (root.saved)
            root.deleteRequested(root.item)
        else
            OnlineAssets.deleteUserItem(root.item)
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
            fillMode: root.isAudio || root.isMogrt ? Image.PreserveAspectFit : Image.PreserveAspectCrop
            sourceSize: Qt.size(Math.round(width * Screen.devicePixelRatio), Math.round(height * Screen.devicePixelRatio))
            visible: status === Image.Ready && !(video.visible)
            opacity: root.isAudio ? 0.9 : 1.0
        }

        // ── video hover preview (stock video, or the preview a .mogrt carries) ──
        readonly property bool hoverVideo: root.kind === "video" || (root.isMogrt && !!root.item.previewUrl)
        MediaPlayer {
            id: player
            source: preview.hoverVideo && root.hovered ? (root.item.previewUrl || "") : ""
            videoOutput: video
            loops: MediaPlayer.Infinite
            audioOutput: AudioOutput { muted: true }
            onSourceChanged: if (source.toString().length > 0) play()
        }
        VideoOutput {
            id: video
            anchors.fill: parent
            anchors.margins: 1
            fillMode: root.isMogrt ? VideoOutput.PreserveAspectFit : VideoOutput.PreserveAspectCrop
            visible: preview.hoverVideo && player.playbackState === MediaPlayer.PlayingState
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
        // ── Dafin: a sound that is a local file drags onto the timeline (imported into Media on the
        //    way, so a saved project keeps it); a click on the waveform plays or stops it. Under
        //    the ▶ and download buttons, which keep their own clicks. ──
        AssetDragSource {
            z: -1
            anchors.fill: parent
            visible: root.localAudioUrl.length > 0
            enabled: visible
            liftEnabled: false
            kind: "media"
            payload: ""
            fileUrl: root.localAudioUrl
            label: root.item.title || ""
            glyph: Theme.icons.music
            onTapped: root.playing = !root.playing
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
            z: 2 // above the drag source of a ready card
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
            glyph: root.kind === "video" ? Theme.icons.film : root.kind === "title" ? Theme.icons.type : Theme.icons.smile
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

        // ── Dafin: a ready card (downloaded, still in Media) drags onto the timeline or the
        //    preview as its downloaded file, like a file from the file manager. That file is
        //    already in Media, so the drop reuses it (no second copy, no second Media row); after a
        //    restart it is imported into Media on the way. A Lottie sticker or title drags even
        //    before the download, as the file its preview plays. ──
        AssetDragSource {
            readonly property string path: root.readyPath.length > 0 ? root.readyPath
                                         : root.isLottie ? root.lottiePath : ""
            anchors.fill: parent
            visible: !root.isMogrt && (root.isLottie || root.readyPath.length > 0)
            enabled: path.length > 0
            liftEnabled: false
            kind: "media"
            payload: ""
            fileUrl: path.length > 0 ? EditorState.fileUrl(path) : ""
            label: root.item.title || ""
            glyph: root.kind === "title" ? Theme.icons.type
                 : root.isAudio ? Theme.icons.music
                 : root.kind === "video" ? Theme.icons.film
                 : Theme.icons.smile
        }

        // ── a template drags onto the timeline like any title; a click adds it at the playhead ──
        AssetDragSource {
            anchors.fill: parent
            visible: root.isMogrt
            enabled: root.isMogrt && root.mogrtAnimates
            kind: "mogrt"
            payload: root.mogrtPath
            label: root.item.title || ""
            thumbnail: root.item.thumbnailUrl || ""
            onTapped: root.addTitle()
        }
        // Templates that can't be animated still open the dialog on a click.
        TapHandler {
            enabled: root.isMogrt && !root.mogrtAnimates
            onTapped: root.addTitle()
        }

        // ── how well the template renders here (top-left) ──
        Rectangle {
            visible: root.isMogrt && (root.mogrtStatus.state || "") !== "ok" && (root.mogrtStatus.state || "") !== ""
            // Under "Not saved" when both show.
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.leftMargin: 5
            anchors.topMargin: root.userItem && !root.saved ? 25 : 5
            radius: 3
            color: root.mogrtStatus.state === "warnings" ? "#c08a1c" : root.mogrtStatus.state === "checking" ? "#b0000000" : "#b03a2b"
            width: statusText.implicitWidth + 8
            height: statusText.implicitHeight + 2
            Text {
                id: statusText
                anchors.centerIn: parent
                color: "white"
                font.pixelSize: Theme.fontSizeXs
                text: root.mogrtStatus.state === "checking" ? qsTr("Testing template…")
                    : root.mogrtStatus.state === "warnings" ? qsTr("Partly supported")
                    : root.mogrtStatus.state === "unsupported" ? qsTr("Not supported")
                    : root.mogrtStatus.state === "needsConverter" ? qsTr("Needs MOGRT Converter")
                    : qsTr("Can't render")
            }
            HoverHandler { id: statusHover }
            ThemedToolTip {
                visible: statusHover.hovered
                text: {
                    const st = root.mogrtStatus
                    const warnings = st.warnings || []
                    if (st.state === "warnings")
                        return qsTr("MOGRT Converter draws this template, but not all of it:") + "\n• " + warnings.join("\n• ")
                    if (st.state === "checking")
                        return qsTr("MOGRT Converter is drawing a test frame to see how much of this template it supports.")
                    return st.message || ""
                }
            }
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
            visible: root.isMogrt ? root.hovered
                                  : root.hovered || root.dlState !== "none" || !root.item.downloadable
            color: root.isMogrt ? (dlHover.hovered ? Theme.primary : "#cc000000")
                 : root.dlState === "done" ? Theme.primary
                 : root.dlState === "failed" ? "#c0392b"
                 : "#cc000000"
            border.color: dlHover.hovered ? "white" : "transparent"
            border.width: 1

            IconGlyph {
                anchors.centerIn: parent
                visible: root.isMogrt || (root.dlState !== "downloading" && root.dlState !== "queued")
                glyph: root.isMogrt ? Theme.icons.plus
                     : !root.item.downloadable ? Theme.icons.lock
                     : root.dlState === "done" ? Theme.icons.check
                     : root.dlState === "failed" ? Theme.icons.refresh
                     : root.userItem ? Theme.icons.plus
                     : Theme.icons.download
                iconSize: 15
                iconColor: !root.isMogrt && root.dlState === "done" ? Theme.primaryForeground : "white"
            }
            CircularProgress {
                anchors.centerIn: parent
                visible: !root.isMogrt && (root.dlState === "downloading" || root.dlState === "queued")
                size: 22
                value: root.dlProgress
                indeterminate: root.dlState === "queued" || root.dlProgress <= 0
            }
            HoverHandler { id: dlHover; cursorShape: root.item.downloadable ? Qt.PointingHandCursor : Qt.ArrowCursor }
            TapHandler {
                // A template's whole card is the button (its drag source takes the click).
                enabled: !root.isMogrt
                onTapped: {
                    if (!root.isMogrt && (root.dlState === "downloading" || root.dlState === "queued"))
                        OnlineAssets.cancelDownload(root.key)
                    else
                        root.startDownload()
                }
            }
            ThemedToolTip {
                visible: dlHover.hovered
                text: root.isMogrt ? (root.mogrtAnimates ? qsTr("Add at the playhead, or drag onto the timeline. Its text and other controls stay editable in Properties › Template.")
                                                        : qsTr("This template can't be animated here. Click for the options."))
                    : !root.item.downloadable ? (root.item.downloadBlockedReason || qsTr("Download not allowed"))
                    : root.dlState === "done" ? (root.userItem ? qsTr("Added. Drag it onto the timeline, or click to add it to Media again.")
                                                               : qsTr("Ready. Drag it onto the timeline, or click to add it to Media again."))
                    : root.dlState === "downloading" ? qsTr("Downloading %1%. Click to cancel.").arg(Math.round(root.dlProgress * 100))
                    : root.dlState === "queued" ? qsTr("Waiting… Click to cancel.")
                    : root.dlState === "failed" ? (root.dlMessage || qsTr("Download failed. Click to retry."))
                    : root.userItem ? qsTr("Add to Media")
                    : qsTr("Download and add to Media")
            }
        }

        // ── the user's own files: "Not saved" mark, Save and Delete ──
        Rectangle {
            id: unsavedBadge
            visible: root.userItem && !root.saved
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 5
            radius: 3
            color: "#d0b9770e"
            width: unsavedText.implicitWidth + 8
            height: unsavedText.implicitHeight + 2
            Text {
                id: unsavedText
                anchors.centerIn: parent
                text: qsTr("Not saved")
                color: "white"
                font.pixelSize: Theme.fontSizeXs
            }
            HoverHandler { id: unsavedHover }
            ThemedToolTip {
                visible: unsavedHover.hovered
                text: qsTr("Only kept until Dafin closes. Click Save to keep it.")
            }
        }
        Row {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 4
            spacing: 4
            visible: root.userItem && (root.hovered || !root.saved)

            Rectangle {
                visible: !root.saved
                width: saveLabel.implicitWidth + 14
                height: 22
                radius: 11
                color: saveHover.hovered ? Qt.lighter(Theme.primary, 1.15) : Theme.primary
                Row {
                    id: saveLabel
                    anchors.centerIn: parent
                    spacing: 3
                    IconGlyph {
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: Theme.icons.save
                        iconSize: 12
                        iconColor: Theme.primaryForeground
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Save")
                        color: Theme.primaryForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.weight: Font.DemiBold
                    }
                }
                HoverHandler { id: saveHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: OnlineAssets.saveUserItem(root.item) }
                ThemedToolTip {
                    visible: saveHover.hovered
                    text: qsTr("Keep it in Dafin until you delete it")
                }
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Save")
            }
            Rectangle {
                visible: root.hovered
                width: 22
                height: 22
                radius: 11
                color: deleteHover.hovered ? "#c0392b" : "#cc000000"
                IconGlyph {
                    anchors.centerIn: parent
                    glyph: Theme.icons.trash
                    iconSize: 12
                    iconColor: "white"
                }
                HoverHandler { id: deleteHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.deleteItem() }
                ThemedToolTip {
                    visible: deleteHover.hovered
                    text: root.saved ? qsTr("Delete from Dafin") : qsTr("Remove")
                }
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Delete")
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
                if (root.userItem) {
                    const extra = root.item.extra || {}
                    if (root.isMogrt) {
                        const fields = (extra.texts || []).length
                        parts.push(fields > 0 ? qsTr("%n text field(s)", "", fields) : qsTr("Template"))
                    } else if (extra.isLink) {
                        parts.push(qsTr("Link"))
                    } else {
                        if (extra.format) parts.push(extra.format)
                        if (extra.sizeLabel) parts.push(extra.sizeLabel)
                    }
                    return parts.join(" · ")
                }
                // Dafin: a library's own label (the built-in effects' sub-category) instead of the
                // author and licence, which are the same on every card there.
                if (root.item.extra && root.item.extra.caption) {
                    parts.push(root.item.extra.caption)
                    if (root.item.extra.mood) parts.push(root.item.extra.mood)
                    return parts.join(" · ")
                }
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
