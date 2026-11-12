import QtQuick
import Drift

// Says the preview is being drawn smaller than asked because playback could not keep up. Export
// quality is not affected. Hidden while the automatic quality is at full.
Rectangle {
    id: badge

    // 1 = Half, 2 = Quarter (PlaybackEngine::adaptiveLevel).
    required property int level

    height: 24
    width: label.implicitWidth + 16
    radius: height / 2
    color: Qt.rgba(0, 0, 0, 0.6)
    border.width: 1
    border.color: Qt.rgba(1, 1, 1, 0.25)

    Text {
        id: label
        anchors.centerIn: parent
        text: badge.level >= 2 ? qsTr("Preview: Quarter") : qsTr("Preview: Half")
        font.pixelSize: 11
        font.bold: true
        color: "white"
    }
}
