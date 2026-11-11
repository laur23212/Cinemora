import QtQuick
import QtQuick.Effects
import QtQuick.Window
import Drift

// Dafin: one project on the start screen's grid — its picture with the edit's length on it, the
// file name and when it was last saved, and a ⋮ menu (Open, Show in folder, Remove). Filmora and
// Premiere Pro projects carry a badge, since they open as a copy rather than as themselves.
Item {
    id: card

    required property var modelData
    property string dateText: ""
    property string durationText: ""
    property string removeLabel: qsTr("Remove from list")

    signal openRequested(string path)
    signal removeRequested(string path)
    signal revealRequested(string path)

    readonly property bool exists: modelData.exists !== false
    readonly property string kind: modelData.kind || "dafin"
    readonly property bool hovered: cardHover.hovered && exists
    readonly property real pictureHeight: Math.round(width * 9 / 16)
    readonly property bool hasPicture: (modelData.thumbnail || "").length > 0
                                       && picture.status === Image.Ready

    implicitHeight: pictureHeight + Theme.spacingMd + textColumn.implicitHeight
    opacity: exists ? 1 : 0.55

    HoverHandler {
        id: cardHover
        cursorShape: card.exists ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    Item {
        id: stage
        width: parent.width
        height: card.pictureHeight

        // The picture is masked rather than clipped so its corners follow the card's rounding.
        Item {
            id: media
            anchors.fill: parent
            visible: false
            layer.enabled: true

            Rectangle {
                anchors.fill: parent
                color: Theme.c.startScreenProjectCard.placeholder
            }

            Image {
                id: picture
                anchors.fill: parent
                source: card.modelData.thumbnail || ""
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: false  // the file is rewritten in place when the project is saved again
                sourceSize.width: Math.ceil(card.width * Screen.devicePixelRatio)
                scale: card.hovered ? 1.04 : 1.0

                Behavior on scale {
                    NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
                }
            }
        }

        Rectangle {
            id: mask
            anchors.fill: parent
            radius: Theme.c.startScreenProjectCard.radius
            visible: false
            layer.enabled: true
        }

        MultiEffect {
            anchors.fill: parent
            source: media
            maskEnabled: true
            maskSource: mask
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1.0
        }

        // No picture yet (a project with nothing on its timeline, media that isn't on this
        // computer, or one still being read): what kind of project it is instead.
        Column {
            anchors.centerIn: parent
            spacing: Theme.spacingSm
            visible: !card.hasPicture

            IconGlyph {
                anchors.horizontalCenter: parent.horizontalCenter
                glyph: Theme.icons.film
                iconSize: Math.max(24, Math.round(card.pictureHeight / 5))
                iconColor: Theme.c.startScreenProjectCard.placeholderIcon
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: card.kind === "filmora" ? qsTr("Filmora project")
                      : card.kind === "premiere" ? qsTr("Premiere Pro project")
                      : qsTr("Dafin project")
                color: Theme.c.startScreenProjectCard.placeholderIcon
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: mask.radius
            color: "transparent"
            border.width: card.hovered ? Theme.c.startScreenProjectCard.borderWidthHover
                                       : Theme.c.startScreenProjectCard.borderWidth
            border.color: card.hovered ? Theme.c.startScreenProjectCard.borderHover
                                       : Theme.c.startScreenProjectCard.border

            Behavior on border.color {
                ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
            }
        }

        // Other editors' projects open as an untitled Dafin copy; the badge says whose they are.
        Rectangle {
            visible: card.kind === "filmora" || card.kind === "premiere"
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: Theme.spacingMd
            width: kindLabel.implicitWidth + Theme.spacingMd * 2
            height: kindLabel.implicitHeight + Theme.spacingXs * 2
            radius: height / 2
            color: card.kind === "filmora" ? Theme.c.startScreenProjectCard.kindFilmora
                                           : Theme.c.startScreenProjectCard.kindPremiere

            Text {
                id: kindLabel
                anchors.centerIn: parent
                text: card.kind === "filmora" ? "Filmora" : "Premiere Pro"
                color: Theme.c.startScreenProjectCard.kindForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.DemiBold
            }
        }

        Rectangle {
            visible: card.durationText.length > 0 || !card.exists
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.spacingMd
            width: durationLabel.implicitWidth + Theme.spacingMd * 2
            height: durationLabel.implicitHeight + Theme.spacingXs * 2
            radius: Theme.radiusMd
            color: Theme.c.startScreenProjectCard.badgeBackground

            Text {
                id: durationLabel
                anchors.centerIn: parent
                text: card.exists ? card.durationText : qsTr("Missing")
                color: Theme.c.startScreenProjectCard.badgeForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                font.weight: Font.Medium
            }
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: (mouse) => {
                if (mouse.button === Qt.RightButton)
                    menu.popup()
                else if (card.exists)
                    card.openRequested(card.modelData.path)
            }
        }
    }

    Item {
        id: textColumn
        anchors.top: stage.bottom
        anchors.topMargin: Theme.spacingMd
        width: parent.width
        implicitHeight: Math.max(nameColumn.implicitHeight, menuButton.height)

        Column {
            id: nameColumn
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingXs
            anchors.right: menuButton.left
            anchors.rightMargin: Theme.spacingSm
            spacing: 2

            Text {
                width: parent.width
                text: card.modelData.name
                color: Theme.c.startScreenProjectCard.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeBase + 2
                font.weight: Font.Medium
                elide: Text.ElideMiddle
            }

            Text {
                width: parent.width
                text: card.dateText
                visible: text.length > 0
                color: Theme.c.startScreenProjectCard.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                elide: Text.ElideRight
            }
        }

        MouseArea {
            anchors.left: parent.left
            anchors.right: menuButton.left
            height: nameColumn.height
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: (mouse) => {
                if (mouse.button === Qt.RightButton)
                    menu.popup()
                else if (card.exists)
                    card.openRequested(card.modelData.path)
            }
        }

        Item {
            id: menuButton
            anchors.right: parent.right
            anchors.top: parent.top
            width: Theme.iconButtonSize
            height: Theme.iconButtonSize

            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusMd
                color: Theme.c.startScreenProjectRow.backgroundHover
                visible: menuArea.containsMouse || menu.opened
            }

            IconGlyph {
                anchors.centerIn: parent
                rotation: 90
                glyph: Theme.icons.ellipsis
                iconSize: Theme.iconSizeMd
                iconColor: menuArea.containsMouse || menu.opened ? Theme.c.startScreenProjectCard.iconHover
                                                                 : Theme.c.startScreenProjectCard.icon
            }

            MouseArea {
                id: menuArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: menu.popup(menuButton, 0, menuButton.height)
            }

            ThemedToolTip {
                text: qsTr("More")
                visible: menuArea.containsMouse && !menu.opened
            }
        }
    }

    ThemedToolTip {
        text: card.exists ? card.modelData.path
                          : qsTr("This file has been moved or deleted:\n%1").arg(card.modelData.path)
        visible: cardHover.hovered && !menuArea.containsMouse && !menu.opened
        delay: 700
    }

    ThemedContextMenu {
        id: menu
        implicitWidth: 220

        ThemedMenuItem {
            text: qsTr("Open")
            icon.name: Theme.icons.folderInput
            enabled: card.exists
            onTriggered: card.openRequested(card.modelData.path)
        }

        ThemedMenuItem {
            text: qsTr("Show in folder")
            icon.name: Theme.icons.folder
            enabled: card.exists
            onTriggered: card.revealRequested(card.modelData.path)
        }

        ThemedMenuSeparator {}

        ThemedMenuItem {
            text: card.removeLabel
            icon.name: Theme.icons.x
            onTriggered: card.removeRequested(card.modelData.path)
        }
    }
}
