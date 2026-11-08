import QtQuick
import QtQuick.Window
import Drift

SettingsPage {
    id: page

    pageId: "about"
    title: qsTr("About")

    SettingsRow {
        title: qsTr("Drift")
        searchId: "about-version"
        description: qsTr("Version %1").arg(Updates.currentVersion)
        query: page.query
    }

    // Dafin: checking for updates has its own page, Settings > Updates.

    SettingsGroupHeader {
        text: qsTr("Help")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Quick Setup")
        searchId: "about-quick-setup"
        description: qsTr("Walk through language, theme, interface size and add-ons again.")
        keywords: "setup first run welcome wizard onboarding language theme scale addons"
        available: EditorState.preferences.canRestart
        query: page.query

        ThemedButton {
            variant: "secondary"
            text: qsTr("Run again")
            onClicked: ShellBridge.main.rerunQuickSetup()
        }
    }

    SettingsRow {
        title: qsTr("About Drift")
        searchId: "about-drift"
        description: qsTr("Open-source video editor by CutWire Studios.")
        keywords: "license credits version"
        query: page.query

        ThemedButton {
            variant: "secondary"
            text: qsTr("Show about dialog")
            onClicked: ShellBridge.main.openAboutDialog()
        }
    }

    SettingsRow {
        title: qsTr("Debug info")
        searchId: "about-debug-info"
        description: qsTr("System, playback and codec details for bug reports.")
        keywords: "diagnostics bug report system codec gpu"
        query: page.query

        ThemedButton {
            variant: "secondary"
            text: qsTr("Open debug info…")
            onClicked: ShellBridge.main.openDebugInfo()
        }
    }
}
