import QtQuick
import Drift

// CapCut/Premiere-style scrubber head in the seek strip.
Item {
    id: playheadHead
    width: Theme.playheadHandleSize
    height: Theme.playheadHandleSize + 2
    x: -width / 2 + Theme.playheadLineWidth / 2
    y: 3

    ThemeSlot {
        slot: "TimelinePlayheadHead"
        control: playheadHead
        anchors.fill: parent
        fallback: Component {
            Item {
                Rectangle {
                    anchors.top: parent.top
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: playheadHead.width
                    height: playheadHead.height * 0.55
                    radius: Theme.c.timelinePlayheadHead.radius
                    color: Theme.c.timelinePlayheadHead.background
                }
                TimelinePlayheadTip { }
            }
        }
    }

    // Scissors chip on the head: click it to cut at the bar (TimelinePlayhead.qml reads the click,
    // the drag area has to stay on top so dragging the head still moves the bar).
    Rectangle {
        id: scissorsChip
        width: 18
        height: 18
        radius: 9
        x: (parent.width - width) / 2
        y: -1
        color: Theme.c.timelinePlayheadHead.background
        IconGlyph {
            anchors.centerIn: parent
            glyph: Theme.icons.scissors
            iconSize: 12
            iconColor: "white"
        }
    }
}
