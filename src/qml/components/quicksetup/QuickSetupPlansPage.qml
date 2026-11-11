import QtQuick
import QtQuick.Window
import Drift

// Last setup page: a fake "Choose your plan" screen that turns into "Just joking" after a
// moment (or as soon as a plan is clicked). Dafin is free; the only way on is the big button.
QuickSetupPage {
    id: root

    property bool revealed: false

    readonly property var plans: [
        { name: qsTr("Hobby"), price: "$20", features: [qsTr("Cuts, but only straight ones"), qsTr("One undo, used wisely"), qsTr("Watermark of your cat")] },
        { name: qsTr("Pro"), price: "$50", features: [qsTr("Cuts at any angle"), qsTr("Unlimited undo (emotional)"), qsTr("Export while you wait")] },
        { name: qsTr("Hollywood"), price: "$100+", features: [qsTr("A director's chair, in spirit"), qsTr("Red carpet on the timeline"), qsTr("Calls us \"darling\"")] }
    ]

    title: qsTr("Choose your plan")
    subtitle: revealed ? "" : qsTr("Pick the one that fits your edits.")
    glyph: Theme.icons.sparkles
    contentMaxWidth: 760
    canAdvance: revealed
    nextLabel: qsTr("Use for free forever")

    function reveal() {
        revealed = true
    }

    Timer {
        interval: 2600
        running: root.visible && !root.revealed
        onTriggered: root.reveal()
    }

    Flow {
        id: cards

        width: parent.width
        spacing: Theme.spacingLg
        opacity: root.revealed ? 0.3 : 1.0

        Behavior on opacity {
            NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
        }

        Repeater {
            model: root.plans

            Rectangle {
                id: plan

                required property var modelData

                width: root.mobile ? cards.width : (cards.width - cards.spacing * 2) / 3
                height: planColumn.height + Theme.spacingXl * 2
                radius: Theme.c.quickSetupCard.radius
                color: planMouse.containsMouse ? Theme.c.quickSetupCard.backgroundHover
                                               : Theme.c.quickSetupCard.background
                border.width: Theme.c.quickSetupCard.borderWidth
                border.color: Theme.c.quickSetupCard.border

                Column {
                    id: planColumn

                    x: Theme.spacingXl
                    y: Theme.spacingXl
                    width: parent.width - Theme.spacingXl * 2
                    spacing: Theme.spacingSm

                    ThemedLabel {
                        width: parent.width
                        tone: "default"
                        size: "base"
                        font.weight: Font.DemiBold
                        text: plan.modelData.name
                    }

                    Text {
                        width: parent.width
                        text: plan.modelData.price
                        color: Theme.c.quickSetup.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeBase * 1.8
                        font.weight: Font.DemiBold
                    }

                    Repeater {
                        model: plan.modelData.features

                        ThemedLabel {
                            required property string modelData

                            width: planColumn.width
                            wrapMode: Text.WordWrap
                            size: "sm"
                            text: modelData
                        }
                    }
                }

                MouseArea {
                    id: planMouse

                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: !root.revealed
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.reveal()
                }
            }
        }
    }

    Item {
        id: reveal

        width: parent.width
        height: jokeColumn.height
        opacity: root.revealed ? 1.0 : 0.0
        scale: root.revealed ? 1.0 : 0.92

        Behavior on opacity {
            NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
        }
        Behavior on scale {
            NumberAnimation { duration: Theme.durationBase; easing.type: Easing.OutBack }
        }

        Column {
            id: jokeColumn

            width: parent.width
            spacing: Theme.spacingLg

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Just joking.")
                color: Theme.c.quickSetup.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeBase * (root.mobile ? 2.2 : 2.6)
                font.weight: Font.Bold
            }

            ThemedLabel {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                size: "sm"
                text: qsTr("Dafin has no plans, no paywall and no watermark.")
            }

            ThemedButton {
                anchors.horizontalCenter: parent.horizontalCenter
                variant: "primary"
                enabled: root.revealed
                text: qsTr("Use for free forever")
                font.pixelSize: Theme.fontSizeBase * 1.3
                leftPadding: Theme.spacing3xl
                rightPadding: leftPadding
                topPadding: Theme.spacingXl
                bottomPadding: topPadding
                onClicked: Window.window.next()
            }
        }
    }
}
