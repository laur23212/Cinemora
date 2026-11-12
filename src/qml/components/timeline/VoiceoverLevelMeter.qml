import QtQuick
import Drift

// Live voice level, green through amber to red.
Item {
    id: meter
    width: 44
    height: 12
    anchors.verticalCenter: parent.verticalCenter

    // Dafin: the raw recorder level jumps every audio buffer, which made the bar flicker.
    // Rises at once, falls at a fixed speed (about 1.5 per second), polled at ~33 fps and only
    // while the bar is on screen.
    property real level: 0
    Timer {
        interval: 30
        repeat: true
        running: meter.visible && (EditorState.audioRecordLevel > 0 || meter.level > 0)
        onTriggered: {
            const target = EditorState.audioRecordLevel
            meter.level = target >= meter.level ? target : Math.max(target, meter.level - 0.045)
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.c.voiceoverLevelMeter.radius
        color: Theme.c.voiceoverLevelMeter.background
        border.color: Theme.c.voiceoverLevelMeter.border
        border.width: Theme.c.voiceoverLevelMeter.borderWidth
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 1
        width: Math.max(1, Math.min(parent.width - 2, (parent.width - 2) * meter.level))
        radius: Theme.c.voiceoverLevelMeter.levelRadius
        color: meter.level > 0.85
               ? Theme.c.voiceoverLevelMeter.levelHot
               : (meter.level > 0.6 ? Theme.c.voiceoverLevelMeter.levelWarm : Theme.c.voiceoverLevelMeter.level)
    }

    ThemedToolTip {
        text: qsTr("Live voice level: %1%").arg(Math.round(EditorState.audioRecordLevel * 100))
        visible: vuMouse.containsMouse
    }

    MouseArea {
        id: vuMouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
    }
}
