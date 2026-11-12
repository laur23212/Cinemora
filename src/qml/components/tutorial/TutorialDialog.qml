pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin Tutorial: a scrollable list of features grouped by category. Clicking one shows its
// summary and a Start guided tour button. A panel's "?" opens straight on its page.
ThemedDialog {
    id: root

    // The feature whose page is shown; null shows the list.
    property var current: null
    // Set by Start guided tour; Main starts the tour once this dialog has closed.
    property var _pendingTour: null

    title: root.current ? root.current.title : qsTr("Tutorial")
    preferredWidth: Theme.dialogWidthMd
    showFooter: false

    // Opens on the list, or on the page of `featureId` when it names a feature.
    function openFor(featureId) {
        Tutorial.load()
        root.current = featureId ? Tutorial.featureById(featureId) : null
        root._pendingTour = null
        root.open()
    }

    onClosed: {
        const f = root._pendingTour
        root._pendingTour = null
        if (f && ShellBridge.main && ShellBridge.main.startTour)
            ShellBridge.main.startTour(f)
    }

    contentItem: Item {
        implicitHeight: Math.min(root.availableContentHeight, 440)
        implicitWidth: Theme.dialogWidthMd

        // --- Feature list ------------------------------------------------------------------
        Flickable {
            id: listFlick
            anchors.fill: parent
            visible: root.current === null
            contentHeight: listColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar { }

            Column {
                id: listColumn
                width: listFlick.width - Theme.spacingLg
                spacing: Theme.spacingXs

                ThemedLabel {
                    width: parent.width
                    visible: Tutorial.categories.length === 0
                    size: "sm"
                    tone: "default"
                    text: qsTr("The tutorial could not be loaded.")
                }

                Repeater {
                    model: Tutorial.categories
                    delegate: Column {
                        id: group
                        required property var modelData
                        width: listColumn.width
                        spacing: 0

                        ThemedLabel {
                            width: parent.width
                            topPadding: Theme.spacingLg
                            bottomPadding: Theme.spacingSm
                            text: group.modelData.name.toUpperCase()
                            font.weight: Font.DemiBold
                        }

                        Repeater {
                            model: group.modelData.items
                            delegate: Rectangle {
                                id: row
                                required property var modelData
                                width: group.width
                                height: Theme.iconButtonSize
                                radius: Theme.radiusSm
                                color: rowHover.hovered ? Theme.c.assetsPanel.optionBackgroundHover
                                                        : "transparent"

                                Text {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.leftMargin: Theme.spacingMd
                                    anchors.rightMargin: Theme.spacingMd
                                    anchors.verticalCenter: parent.verticalCenter
                                    elide: Text.ElideRight
                                    text: row.modelData.title
                                    color: Theme.c.themedLabel.foreground
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeBase
                                }

                                HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
                                TapHandler { onTapped: root.current = row.modelData }

                                Accessible.role: Accessible.Button
                                Accessible.name: row.modelData.title
                            }
                        }
                    }
                }
            }
        }

        // --- One feature -------------------------------------------------------------------
        Item {
            anchors.fill: parent
            visible: root.current !== null

            Column {
                id: detail
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                spacing: Theme.spacingLg

                ThemedLabel {
                    width: parent.width
                    text: root.current ? root.current.category : ""
                }

                ThemedLabel {
                    width: parent.width
                    size: "base"
                    tone: "default"
                    text: root.current ? root.current.summary : ""
                }
            }

            Row {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                spacing: Theme.spacingMd

                ThemedButton {
                    text: qsTr("All features")
                    onClicked: root.current = null
                }
                ThemedButton {
                    text: qsTr("Start guided tour")
                    variant: "primary"
                    enabled: root.current !== null && root.current.tour.length > 0
                    onClicked: {
                        root._pendingTour = root.current
                        root.close()
                    }
                }
            }
        }
    }
}
