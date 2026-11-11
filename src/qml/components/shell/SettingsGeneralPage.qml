import QtQuick
import Drift

SettingsPage {
    id: page

    pageId: "general"
    title: qsTr("General")

    function cacheSizeText(bytes) {
        const gib = 1024 * 1024 * 1024
        if (bytes >= gib)
            return qsTr("%1 GB").arg((bytes / gib).toFixed(1))
        return qsTr("%1 MB").arg((bytes / (1024 * 1024)).toFixed(1))
    }

    SettingsGroupHeader {
        text: qsTr("Language & region")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Language")
        searchId: "general-language"
        description: qsTr("Language for menus and labels. Takes effect immediately.")
        keywords: "locale translation"
        query: page.query

        ThemedComboBox {
            width: Theme.c.settingsRow.controlWidth
            textRole: "label"
            valueRole: "id"
            model: EditorState.preferences.uiLanguages
            currentIndex: {
                const langs = EditorState.preferences.uiLanguages
                for (var i = 0; i < langs.length; ++i) {
                    if (langs[i].id === EditorState.preferences.uiLanguage)
                        return i
                }
                return 0
            }
            onActivated: {
                const langs = EditorState.preferences.uiLanguages
                if (currentIndex >= 0 && currentIndex < langs.length)
                    EditorState.preferences.uiLanguage = langs[currentIndex].id
            }
        }
    }

    SettingsGroupHeader {
        text: qsTr("Startup")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Reopen last project on startup")
        searchId: "general-reopen-last-project"
        description: qsTr("Restores the last open project. Closing still asks you to save.")
        query: page.query

        ThemedSwitch {
            checked: EditorState.preferences.reopenLastProject
            onToggled: EditorState.preferences.reopenLastProject = checked
        }
    }

    // Dafin: the start screen's "On this computer" tab (ProjectScanner).
    SettingsRow {
        title: qsTr("Find projects on this computer")
        searchId: "general-find-projects"
        description: {
            const scanner = EditorState.projectFile.scanner
            const names = scanner ? scanner.defaultFolders.map(p => p.split(/[\\/]/).pop()) : []
            return qsTr("The start screen lists Dafin, Filmora (.wfp) and Premiere Pro (.prproj) projects found in %1 and in the folders below. Filmora and Premiere projects open as a new Dafin project; the original is never changed.")
                       .arg(names.length > 0 ? names.join(", ") : qsTr("your usual folders"))
        }
        keywords: "start screen recent projects filmora premiere wfp prproj scan search folders"
        query: page.query

        ThemedSwitch {
            checked: EditorState.projectFile.scanner ? EditorState.projectFile.scanner.enabled : false
            onToggled: if (EditorState.projectFile.scanner) EditorState.projectFile.scanner.enabled = checked
        }
    }

    SettingsRow {
        id: projectFoldersRow
        readonly property var scanner: EditorState.projectFile.scanner
        title: qsTr("Project folders")
        searchId: "general-project-folders"
        description: qsTr("More places to look, such as a projects drive. Each is searched six folders deep.")
        keywords: "start screen projects folders scan add remove"
        query: page.query
        stacked: true
        available: !!scanner && scanner.enabled

        Column {
            width: parent ? parent.width : 0
            spacing: Theme.spacingSm

            Repeater {
                model: projectFoldersRow.scanner ? projectFoldersRow.scanner.folders : []

                delegate: Rectangle {
                    required property string modelData
                    width: parent ? parent.width : 0
                    height: 36
                    radius: Theme.c.startScreenProjectRow.radius
                    color: Theme.c.startScreenProjectRow.background
                    border.width: Theme.c.startScreenProjectRow.borderWidth
                    border.color: Theme.c.startScreenProjectRow.border

                    IconGlyph {
                        id: folderGlyph
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.spacingMd
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: Theme.icons.folder
                        iconSize: Theme.iconSizeMd
                        iconColor: Theme.c.startScreenProjectRow.icon
                    }

                    Text {
                        anchors.left: folderGlyph.right
                        anchors.leftMargin: Theme.spacingMd
                        anchors.right: removeFolder.left
                        anchors.rightMargin: Theme.spacingMd
                        anchors.verticalCenter: parent.verticalCenter
                        text: parent.modelData
                        color: Theme.c.startScreenProjectRow.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                        elide: Text.ElideMiddle
                    }

                    IconButton {
                        id: removeFolder
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.spacingXs
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: Theme.icons.x
                        tooltip: qsTr("Stop searching this folder")
                        onClicked: projectFoldersRow.scanner.removeFolder(parent.modelData)
                    }
                }
            }

            Row {
                spacing: Theme.spacingSm

                ThemedButton {
                    variant: "secondary"
                    glyph: Theme.icons.plus
                    text: qsTr("Add folder…")
                    onClicked: {
                        const url = FileDialogs.openDirectory(qsTr("Add a folder to search for projects"))
                        if (url && url.toString().length > 0 && !projectFoldersRow.scanner.addFolder(url))
                            Toasts.error(qsTr("Couldn’t use that folder."))
                    }
                }

                ThemedButton {
                    variant: "ghost"
                    glyph: Theme.icons.reset
                    text: qsTr("Show removed projects again")
                    tooltip: qsTr("Projects removed from the start screen's “On this computer” list come back")
                    onClicked: projectFoldersRow.scanner.unhideAll()
                }
            }
        }
    }

    SettingsGroupHeader {
        text: qsTr("Saving")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Autosave interval")
        searchId: "general-autosave-interval"
        description: qsTr("How often unsaved work is snapshotted for crash recovery.")
        keywords: "recovery backup"
        query: page.query

        ThemedComboBox {
            width: Theme.c.settingsRow.controlWidth
            textRole: "label"
            valueRole: "id"
            model: [
                { id: 0,   label: qsTr("Off") },
                { id: 15,  label: qsTr("Every 15 seconds") },
                { id: 30,  label: qsTr("Every 30 seconds") },
                { id: 60,  label: qsTr("Every minute") },
                { id: 300, label: qsTr("Every 5 minutes") }
            ]
            currentIndex: {
                const opts = model
                for (var i = 0; i < opts.length; ++i) {
                    if (opts[i].id === EditorState.preferences.autosaveIntervalSeconds)
                        return i
                }
                return 0
            }
            onActivated: {
                if (currentIndex >= 0 && currentIndex < model.length)
                    EditorState.preferences.autosaveIntervalSeconds = model[currentIndex].id
            }
        }
    }

    SettingsGroupHeader {
        text: qsTr("Extras")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Remind about essential packs")
        searchId: "general-remind-essential-packs"
        description: qsTr("Pulse the Extras icon when the video, transitions, and audio packs are not installed.")
        query: page.query

        ThemedSwitch {
            checked: Addons.remindEssential
            onToggled: Addons.remindEssential = checked
        }
    }

    SettingsRow {
        title: qsTr("Remind about pack updates")
        searchId: "general-remind-pack-updates"
        description: qsTr("Pulse the Extras icon when updates are available for packs you already have installed.")
        query: page.query

        ThemedSwitch {
            checked: Addons.remindUpdates
            onToggled: Addons.remindUpdates = checked
        }
    }

    SettingsGroupHeader {
        text: qsTr("Feedback")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Haptic feedback")
        searchId: "general-haptic-feedback"
        description: qsTr("Vibrate on taps, snaps, and edits. Uses this device’s own haptic effects when it has them.")
        available: Haptics.supported
        query: page.query

        ThemedSwitch {
            checked: Haptics.enabled
            onToggled: Haptics.enabled = checked
        }
    }

    SettingsGroupHeader {
        text: qsTr("Storage")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Cache")
        searchId: "general-cache"
        description: qsTr("%1 used").arg(page.cacheSizeText(EditorState.preferences.cacheSizeBytes))
        keywords: "disk space thumbnails proxies"
        query: page.query
        Component.onCompleted: EditorState.preferences.refreshCacheSize()

        ThemedButton {
            variant: "secondary"
            text: qsTr("Clear cache")
            enabled: !EditorState.preferences.cacheBusy
            onClicked: EditorState.preferences.clearCache()
        }
    }

    SettingsRow {
        title: qsTr("Cache location")
        searchId: "general-cache-location"
        description: EditorState.preferences.cacheLocationPendingRestart
                         ? qsTr("%1. Takes effect after restart.").arg(EditorState.preferences.cacheLocation)
                         : EditorState.preferences.cacheLocation
        keywords: "cache folder directory path storage"
        available: Qt.platform.os !== "android" && Qt.platform.os !== "wasm"
        query: page.query

        ThemedButton {
            variant: "secondary"
            text: qsTr("Reset")
            visible: EditorState.preferences.cacheLocationCustom
            onClicked: EditorState.preferences.resetCacheLocation()
        }

        ThemedButton {
            variant: "secondary"
            text: qsTr("Change…")
            onClicked: {
                const url = FileDialogs.openDirectory(qsTr("Cache location"))
                if (url && url.toString() !== "")
                    EditorState.preferences.setCacheLocation(url)
            }
        }
    }

    SettingsGroupHeader {
        text: qsTr("Settings file")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Export and import")
        searchId: "general-export-import"
        description: qsTr("Save your preferences to a JSON file, or load them from one.")
        keywords: "backup restore"
        query: page.query

        Row {
            spacing: Theme.spacingMd

            ThemedButton {
                variant: "secondary"
                text: qsTr("Export settings…")
                onClicked: {
                    const url = FileDialogs.saveFile(qsTr("Export Settings"), [qsTr("JSON document (*.json)")],
                                                     "drift-settings", "json", "", ["application/json"])
                    if (url == "")
                        return
                    const error = EditorState.preferences.exportSettings(url)
                    EditorState.setLastMessage(error || qsTr("Settings exported"), error ? "error" : "success")
                }
            }

            ThemedButton {
                variant: "secondary"
                text: qsTr("Import settings…")
                onClicked: {
                    const url = FileDialogs.openFile(qsTr("Import Settings"), [qsTr("JSON document (*.json)")],
                                                     ["application/json"])
                    if (url == "")
                        return
                    const error = EditorState.preferences.importSettings(url)
                    if (error)
                        EditorState.setLastMessage(error, "error")
                }
            }
        }
    }
}
