import QtQuick
import Drift

// One project in the start screen's list view: a small picture, the name, and when it was saved,
// how long it runs and (for Filmora / Premiere Pro projects) whose it is. Show in folder and
// remove appear on hover.
Rectangle {
    id: projectRow

    required property var modelData
    property string dateText: ""
    property string durationText: ""
    property string removeLabel: qsTr("Remove from list")

    signal openRequested(string path)
    signal removeRequested(string path)
    signal revealRequested(string path)

    readonly property bool exists: modelData.exists !== false
    readonly property bool hovered: rowHover.hovered
    readonly property string kind: modelData.kind || "dafin"
    readonly property string details: {
        const parts = []
        if (dateText.length > 0)
            parts.push(dateText)
        if (durationText.length > 0)
            parts.push(durationText)
        if (kind === "filmora")
            parts.push("Filmora")
        else if (kind === "premiere")
            parts.push("Premiere Pro")
        if (!exists)
            parts.push(qsTr("missing"))
        return parts.join("  ·  ")
    }

    width: ListView.view ? ListView.view.width : 0
    height: 64
    radius: Theme.c.startScreenProjectRow.radius
    color: hovered && exists ? Theme.c.startScreenProjectRow.backgroundHover
                             : Theme.c.startScreenProjectRow.background
    border.width: Theme.c.startScreenProjectRow.borderWidth
    border.color: Theme.c.startScreenProjectRow.border
    opacity: exists ? 1 : 0.6

    Behavior on color {
        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    HoverHandler {
        id: rowHover
    }

    Row {
        anchors.left: parent.left
        anchors.right: rowButtons.left
        anchors.leftMargin: Theme.spacingMd
        anchors.rightMargin: Theme.spacingLg
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingLg

        Rectangle {
            id: thumb
            width: 76
            height: 43
            radius: Theme.radiusSm
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.c.startScreenProjectCard.placeholder
            clip: true

            Image {
                id: thumbImage
                anchors.fill: parent
                source: projectRow.modelData.thumbnail || ""
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: false
                sourceSize.width: 152
            }

            IconGlyph {
                anchors.centerIn: parent
                visible: thumbImage.status !== Image.Ready
                glyph: Theme.icons.film
                iconSize: Theme.iconSizeMd
                iconColor: Theme.c.startScreenProjectCard.placeholderIcon
            }
        }

        Column {
            width: parent.width - thumb.width - Theme.spacingLg
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            Text {
                width: parent.width
                text: projectRow.modelData.name
                color: Theme.c.startScreenProjectRow.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeBase
                font.weight: Font.Medium
                elide: Text.ElideMiddle
            }

            Text {
                width: parent.width
                text: projectRow.details
                color: Theme.c.startScreenProjectRow.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                elide: Text.ElideRight
            }
        }
    }

    ThemedToolTip {
        text: projectRow.exists
              ? projectRow.modelData.path
              : qsTr("This file has been moved or deleted:\n%1").arg(projectRow.modelData.path)
        visible: projectRow.hovered && !revealArea.containsMouse && !removeArea.containsMouse
    }

    // Below the buttons so a click on one of them never also opens the project.
    MouseArea {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: rowButtons.left
        cursorShape: projectRow.exists ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: {
            if (projectRow.exists)
                projectRow.openRequested(projectRow.modelData.path)
        }
    }

    Row {
        id: rowButtons
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingSm
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        opacity: projectRow.hovered ? 1 : 0

        Behavior on opacity {
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        Item {
            width: Theme.iconSizeMd + Theme.spacingSm * 2
            height: width
            visible: projectRow.exists

            IconGlyph {
                anchors.centerIn: parent
                glyph: Theme.icons.folder
                iconSize: Theme.iconSizeMd
                iconColor: revealArea.containsMouse ? Theme.c.startScreenProjectRow.iconHover
                                                    : Theme.c.startScreenProjectRow.icon
            }

            ThemedToolTip {
                text: qsTr("Show in folder")
                visible: revealArea.containsMouse
            }

            MouseArea {
                id: revealArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: projectRow.revealRequested(projectRow.modelData.path)
            }
        }

        Item {
            width: Theme.iconSizeMd + Theme.spacingSm * 2
            height: width

            IconGlyph {
                anchors.centerIn: parent
                glyph: Theme.icons.x
                iconSize: Theme.iconSizeMd
                iconColor: removeArea.containsMouse ? Theme.c.startScreenProjectRow.iconHover
                                                    : Theme.c.startScreenProjectRow.icon
            }

            ThemedToolTip {
                text: projectRow.removeLabel
                visible: removeArea.containsMouse
            }

            MouseArea {
                id: removeArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: projectRow.removeRequested(projectRow.modelData.path)
            }
        }
    }
}
