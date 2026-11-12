import QtQuick
import Drift
// Dafin: hover scrub. Shows the frame of a video at a position across the item, so moving the
// mouse over a media-library card or a timeline clip skims through it without playing.
//
// Light by design: nothing is decoded here. The first picture is a frame of the video's cached
// 8-frame filmstrip (already on disk and in the image cache); the sharp frame comes from the
// on-demand tile cache the timeline filmstrip uses (FilmstripTileCache), asked for only once the
// pointer has rested a moment and only at about 24 positions per video, so a sweep of the mouse
// never queues dozens of decodes and every tile is decoded once, ever. When neither picture is
// ready yet the item stays invisible, so the normal thumbnail shows through.
Item {
    id: root

    // The cached coarse strip of the video (asset.filmstripPath / clip.filmstripPath).
    property string filmstripPath: ""
    // The media file, for the sharp on-demand frames. Empty: coarse strip frames only.
    property string sourcePath: ""
    // Source window the full width of this item stands for, in seconds. A media-library card
    // passes 0 .. duration; a timeline clip its in and out points.
    property real inPoint: 0
    property real outPoint: 0
    // Length of the whole source: the coarse strip spans all of it.
    property real sourceDuration: 0
    property int rotationCorrection: 0
    // Pointer position across the item, 0 .. 1.
    property real fraction: 0
    // Run only while the pointer is over the item.
    property bool active: false
    // Show the thin position line (the library card has no other cue).
    property bool showLine: true

    readonly property int coarseFrameCount: 8
    readonly property bool scrubbing: active && outPoint > inPoint && fraction >= 0
    readonly property real sourceSeconds: scrubbing
        ? inPoint + Math.max(0, Math.min(1, fraction)) * (outPoint - inPoint) : 0
    readonly property real wholeDuration: sourceDuration > 0 ? sourceDuration : outPoint
    readonly property int coarseIndex: wholeDuration > 0
        ? Math.max(0, Math.min(coarseFrameCount - 1,
                               Math.floor(sourceSeconds / wholeDuration * coarseFrameCount))) : 0
    // About 24 positions across the window, on the power-of-two second grid the tile cache uses.
    readonly property int tileLevel: Math.max(-3, Math.min(14, Math.floor(
        Math.log(Math.max(0.05, (outPoint - inPoint) / 24)) / Math.LN2)))
    readonly property int tileIndex: Math.floor(sourceSeconds / Math.pow(2, tileLevel))

    property string sharpUrl: ""

    function refreshSharp() {
        sharpUrl = (scrubbing && sourcePath.length > 0)
            ? EditorState.filmstripTileUrl(sourcePath, tileLevel, tileIndex, rotationCorrection) : ""
    }

    // A new position: drop the old sharp frame at once (it would be the wrong picture) and ask
    // for the new one when the pointer settles.
    onTileIndexChanged: { sharpUrl = ""; settle.restart() }
    onScrubbingChanged: { sharpUrl = ""; if (scrubbing) settle.restart(); else settle.stop() }

    visible: scrubbing && (coarse.status === Image.Ready || sharp.status === Image.Ready)
    clip: true

    Timer {
        id: settle
        interval: 90
        onTriggered: root.refreshSharp()
    }

    Connections {
        target: EditorState
        // A decode landed: the tile asked for a moment ago may exist now.
        function onFilmstripTileReady(path) {
            if (root.scrubbing && path === root.sourcePath)
                root.refreshSharp()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
    }

    Image {
        id: coarse
        anchors.fill: parent
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        cache: true
        source: root.scrubbing && root.filmstripPath.length > 0
                ? EditorState.filmstripFrameUrl(root.filmstripPath, root.coarseIndex, root.coarseFrameCount)
                : ""
    }

    Image {
        id: sharp
        anchors.fill: parent
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        cache: true
        visible: status === Image.Ready
        source: root.sharpUrl
    }

    Rectangle {
        visible: root.showLine
        width: 2
        height: parent.height
        x: Math.max(0, Math.min(parent.width - width, root.fraction * parent.width - width / 2))
        color: Qt.rgba(1, 1, 1, 0.85)
    }
}
