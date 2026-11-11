import QtQuick
import Drift

// Shared style-pack thumbnail: dark canvas so light glyphs stay legible in both themes.
//
// Dafin: while hovered the card plays the title — entrance, hold and exit over three seconds,
// looping — drawn by the same text painter as the timeline (image://textstyle/<id>?t=…). Two
// images take turns so a frame never blinks while the next one renders, as on sticker cards.
Item {
    id: root

    property string presetId: ""
    property bool selected: false
    property bool hovered: false
    // Off where a card is too small to read the motion, or many cards would animate at once.
    property bool animateOnHover: true
    // Bumped by the owner when the template behind presetId changes, so stale frames are dropped.
    property int revision: 0

    // Landscape cards match the properties picker; assets can size freely.
    implicitWidth: 120
    implicitHeight: Math.round(implicitWidth * 0.5)

    readonly property real loopSeconds: 3.0
    readonly property real frameStep: 1 / 15
    readonly property bool playing: root.animateOnHover && root.hovered && root.presetId.length > 0
    property real playT: 0
    property bool frontIsA: true

    function frameUrl(t) {
        return "image://textstyle/" + root.presetId + "?t=" + t.toFixed(3) + "&rev=" + root.revision
    }

    onPlayingChanged: {
        if (playing) {
            // Starts a little in, so the first frame already shows text.
            root.playT = 0.4
            root.frontIsA = true
            frameA.source = root.frameUrl(root.playT)
        } else {
            frameA.source = ""
            frameB.source = ""
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.c.textStylePackThumb.radius
        color: Theme.c.textStylePackThumb.background
        border.width: root.selected ? Theme.c.textStylePackThumb.borderWidthFocus : Theme.c.textStylePackThumb.borderWidth
        border.color: root.selected ? Theme.c.textStylePackThumb.borderSelected
                                    : (root.hovered ? Theme.c.textStylePackThumb.borderHover : Theme.c.textStylePackThumb.border)
        clip: true

        Behavior on border.color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        Image {
            id: still
            anchors.fill: parent
            anchors.margins: 4
            visible: root.presetId.length > 0
                     && !(root.playing && (frameA.status === Image.Ready || frameB.status === Image.Ready))
            asynchronous: true
            fillMode: Image.Pad
            source: root.presetId.length > 0 ? ("image://textstyle/" + root.presetId) : ""
            sourceSize.width: Math.max(1, Math.round(width))
            sourceSize.height: Math.max(1, Math.round(height))
        }

        Image {
            id: frameA
            anchors.fill: still
            asynchronous: true
            cache: false
            fillMode: Image.Pad
            sourceSize: still.sourceSize
            visible: root.playing && root.frontIsA && status === Image.Ready
            onStatusChanged: if (status === Image.Ready && !root.frontIsA) root.frontIsA = true
        }
        Image {
            id: frameB
            anchors.fill: still
            asynchronous: true
            cache: false
            fillMode: Image.Pad
            sourceSize: still.sourceSize
            visible: root.playing && !root.frontIsA && status === Image.Ready
            onStatusChanged: if (status === Image.Ready && root.frontIsA) root.frontIsA = false
        }

        Timer {
            interval: Math.round(root.frameStep * 1000)
            repeat: true
            running: root.playing
            onTriggered: {
                const back = root.frontIsA ? frameB : frameA
                if (back.status === Image.Loading)
                    return
                // Whole frames, so each loop asks for the same instants and finds them cached.
                const next = Math.round((root.playT + root.frameStep) / root.frameStep) * root.frameStep
                root.playT = next >= root.loopSeconds ? 0 : next
                back.source = root.frameUrl(root.playT)
            }
        }

        Text {
            anchors.centerIn: parent
            visible: root.presetId.length === 0
            text: qsTr("Custom")
            color: Theme.c.textStylePackThumb.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }
}
