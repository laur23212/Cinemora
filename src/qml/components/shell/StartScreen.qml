import QtQuick
import Drift

// Shown at startup in place of a fresh empty project whenever "Reopen last project
// on startup" is off: what you were working on, and how to start something. The
// mobile equivalent is AndroidProjectsPage.
//
// Dafin: laid out like Filmora's launcher — a big title and buttons, then the projects as cards
// under two tabs: Recent Projects (what Dafin opened) and On this computer (Dafin, Filmora and
// Premiere Pro projects ProjectScanner finds in the usual folders). Grid or list, and Refresh.
Item {
    id: root

    signal newProjectRequested()
    signal openProjectRequested()
    signal openRecentRequested(string path)
    // The header's app-level entries, which do not need a project: without them here,
    // turning on agent access or changing a setting meant creating an empty project first.
    signal helpRequested()
    signal updatesRequested()
    signal moreSettingsRequested()
    signal debugInfoRequested()
    signal agentAccessRequested()
    // Dafin: Settings › General, where the searched folders are chosen.
    signal projectFoldersRequested()

    ThemeSlot {
        anchors.fill: parent
        slot: "StartScreenBackground"
        control: root
        fallback: Component {
            Rectangle {
                anchors.fill: parent
                color: Theme.c.startScreen.background
            }
        }
    }

    readonly property var items: EditorState.projectFile.recentProjects
    readonly property var scanner: EditorState.projectFile.scanner
    readonly property var found: scanner ? scanner.projects : []
    readonly property bool scanning: scanner ? scanner.scanning : false
    readonly property string view: scanner ? scanner.startView : "grid"
    // Recents first when there are any; a first start lands on what was found on disk.
    property string tab: items.length > 0 ? "recent" : "found"

    Component.onCompleted: {
        if (root.scanner)
            root.scanner.rescan()
    }

    // Same order, icons and labels as the editor header, so they read as the same controls.
    Row {
        id: appActions
        z: 1
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: Theme.spacingXl
        spacing: Theme.spacingSm

        // The header's own Settings menu, minus Multicam, which needs a timeline.
        IconButton {
            id: settingsButton
            glyph: Theme.icons.settings
            variant: "ghost"
            text: qsTr("Settings")
            active: settingsMenu.opened
            tooltip: qsTr("Settings")
            onClicked: settingsMenu.popup(0, settingsButton.height + Theme.spacingMd)

            AppSettingsMenu {
                id: settingsMenu
                projectTools: false
                onMoreSettingsRequested: root.moreSettingsRequested()
                onHelpRequested: root.helpRequested()
                onUpdatesRequested: root.updatesRequested()
            }
        }

        IconButton {
            glyph: Theme.icons.bug
            variant: "ghost"
            text: qsTr("Debug info")
            tooltip: qsTr("System, playback and codec details for bug reports")
            onClicked: root.debugInfoRequested()
        }

        IconButton {
            glyph: Theme.icons.bot
            variant: "ghost"
            text: qsTr("Agent")
            active: EditorState.mcp.running
            tooltip: EditorState.mcp.running
                     ? qsTr("Agent access is on")
                     : qsTr("Agent access")
            onClicked: root.agentAccessRequested()
        }
    }

    component Tab: Item {
        id: tabItem
        property string label
        property bool current: false
        property bool busy: false
        signal activated()

        width: tabLabel.implicitWidth + (busy ? busyGlyph.width + Theme.spacingSm : 0)
        height: parent.height

        Text {
            id: tabLabel
            anchors.verticalCenter: parent.verticalCenter
            text: tabItem.label
            color: tabItem.current || tabArea.containsMouse ? Theme.c.startScreenTabs.foregroundActive
                                                            : Theme.c.startScreenTabs.foreground
            font.family: Theme.fontFamily
            font.pixelSize: 20
            font.weight: tabItem.current ? Font.DemiBold : Font.Medium
        }

        IconGlyph {
            id: busyGlyph
            visible: tabItem.busy
            anchors.left: tabLabel.right
            anchors.leftMargin: Theme.spacingSm
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.spinner
            iconSize: Theme.iconSizeMd
            iconColor: Theme.c.startScreenTabs.foreground
            spinning: tabItem.busy
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 3
            radius: 1.5
            color: Theme.c.startScreenTabs.indicator
            visible: tabItem.current
        }

        MouseArea {
            id: tabArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: tabItem.activated()
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.y + column.implicitHeight + Theme.spacing3xl
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick

        Column {
            id: column
            width: Math.min(1060, flick.width - Theme.spacing3xl * 2)
            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.max(Theme.spacing3xl, Math.min(flick.height * 0.12, 112))
            spacing: Theme.spacing3xl

            Column {
                width: parent.width
                spacing: Theme.spacing2xl + Theme.spacingSm

                StartScreenTitle {
                    width: parent.width
                }

                StartScreenActions {
                    width: parent.width
                    onNewProjectRequested: root.newProjectRequested()
                    onOpenProjectRequested: root.openProjectRequested()
                }
            }

            Column {
                width: parent.width
                spacing: Theme.spacing2xl

                Item {
                    id: tabBar
                    width: parent.width
                    height: 48

                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 1
                        width: parent.width
                        height: 1
                        color: Theme.c.startScreenTabs.rule
                    }

                    Row {
                        height: parent.height
                        spacing: Theme.spacing2xl + Theme.spacingMd

                        Tab {
                            label: qsTr("Recent Projects")
                            current: root.tab === "recent"
                            onActivated: root.tab = "recent"
                        }

                        Tab {
                            label: qsTr("On this computer")
                            current: root.tab === "found"
                            busy: root.scanning
                            onActivated: root.tab = "found"
                        }
                    }

                    Row {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.verticalCenterOffset: -2
                        spacing: Theme.spacingSm

                        IconButton {
                            glyph: Theme.icons.layoutGrid
                            active: root.view !== "list"
                            tooltip: qsTr("Grid")
                            onClicked: if (root.scanner) root.scanner.startView = "grid"
                        }

                        IconButton {
                            glyph: Theme.icons.list
                            active: root.view === "list"
                            tooltip: qsTr("List")
                            onClicked: if (root.scanner) root.scanner.startView = "list"
                        }

                        IconButton {
                            glyph: Theme.icons.refresh
                            enabled: !root.scanning
                            tooltip: root.scanning ? qsTr("Looking for projects…")
                                                   : qsTr("Look for projects again")
                            onClicked: if (root.scanner) root.scanner.rescan()
                        }
                    }
                }

                StartScreenRecents {
                    width: parent.width
                    items: root.tab === "recent" ? root.items : root.found
                    view: root.view
                    removeLabel: root.tab === "recent" ? qsTr("Remove from recents")
                                                       : qsTr("Remove from this list")
                    emptyText: root.tab === "recent"
                               ? qsTr("Nothing here yet — projects you save will show up here.")
                               : root.scanning
                                 ? qsTr("Looking for projects…")
                                 : (root.scanner && !root.scanner.enabled)
                                   ? qsTr("Looking for projects on this computer is turned off.")
                                   : qsTr("No Dafin, Filmora or Premiere Pro projects found in your usual folders.")
                    onOpenRecentRequested: (path) => root.openRecentRequested(path)
                    onRevealRequested: (path) => { if (root.scanner) root.scanner.revealInFolder(path) }
                    onRemoveRequested: (path) => {
                        if (root.tab === "recent")
                            EditorState.projectFile.removeRecentProject(path)
                        else if (root.scanner)
                            root.scanner.hide(path)
                    }
                }

                // Where "On this computer" looks, and the way to change it.
                Row {
                    visible: root.tab === "found"
                    spacing: Theme.spacingSm

                    ThemedLabel {
                        anchors.verticalCenter: parent.verticalCenter
                        size: "sm"
                        text: root.scanner && root.scanner.folders.length > 0
                              ? qsTr("Searched: Documents, Videos, Desktop, Downloads and your project folders.")
                              : qsTr("Searched: Documents, Videos, Desktop and Downloads.")
                    }

                    ThemedButton {
                        variant: "ghost"
                        glyph: Theme.icons.folderInput
                        text: qsTr("Project folders…")
                        onClicked: root.projectFoldersRequested()
                    }
                }
            }
        }
    }
}
