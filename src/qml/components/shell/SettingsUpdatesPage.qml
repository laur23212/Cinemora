import QtQuick
import Drift

// Settings > Updates (Dafin). The installed app looks for a newer release of Dafin on GitHub
// (laur23212/Dafin): "Check now" asks right away, and a release that is found can be downloaded
// and installed from here — the installer is checked against its SHA-256, runs when Dafin
// restarts, and starts Dafin again. The portable copy never updates itself; it points at the
// releases page instead.
SettingsPage {
    id: page

    pageId: "updates"
    title: qsTr("Updates")

    readonly property string phase: Updates.downloading ? "downloading"
                                    : Updates.preparing ? "preparing"
                                    : Updates.readyToInstall ? "ready"
                                    : Updates.updateAvailable ? "available"
                                    : "idle"

    SettingsRow {
        title: qsTr("Installed version")
        searchId: "updates-version"
        description: qsTr("Drift %1").arg(Updates.currentVersion)
        keywords: "version installed current"
        query: page.query
    }

    SettingsRow {
        title: qsTr("Check for updates")
        searchId: "updates-check"
        description: {
            if (Updates.checking)
                return qsTr("Checking for updates…")
            if (Updates.error.length > 0)
                return Updates.error
            if (Updates.updateAvailable)
                return qsTr("Drift %1 is available.").arg(Updates.latestVersion)
            if (Updates.status.length > 0)
                return Updates.status
            return qsTr("Looks on GitHub for a newer Drift and offers to install it.")
        }
        keywords: "update check now version release newer download install github"
        available: Updates.supported
        query: page.query

        ThemedButton {
            variant: "secondary"
            glyph: Theme.icons.refresh
            text: Updates.checking ? qsTr("Checking…") : qsTr("Check now")
            enabled: !Updates.checking && !Updates.downloading && !Updates.preparing
            onClicked: Updates.checkNow()
        }
    }

    // The release that was found: download it, then restart into it.
    SettingsRow {
        title: qsTr("New version")
        searchId: "updates-install"
        description: {
            switch (page.phase) {
            case "downloading":
                return qsTr("Downloading Drift %1…").arg(Updates.latestVersion)
            case "preparing":
                return qsTr("Preparing the update…")
            case "ready":
                return Updates.installScheduled
                        ? qsTr("Drift %1 will install when you close Drift.").arg(Updates.latestVersion)
                        : qsTr("Drift %1 is downloaded. Restart Drift to install it; you can save your work first.")
                              .arg(Updates.latestVersion)
            default:
                return Updates.canInstall
                        ? qsTr("Drift %1: downloads the installer from GitHub and installs it when Drift restarts.")
                              .arg(Updates.latestVersion)
                        : qsTr("Drift %1: open the release page to download it.").arg(Updates.latestVersion)
            }
        }
        keywords: "update install download restart new version"
        available: Updates.supported && Updates.updateAvailable
        query: page.query

        Row {
            spacing: Theme.spacingSm

            ThemedButton {
                variant: "ghost"
                text: qsTr("What's new")
                visible: Updates.releaseNotes.length > 0
                onClicked: ShellBridge.main.openUpdateDialog()
            }

            ThemedButton {
                variant: "primary"
                glyph: page.phase === "ready" ? Theme.icons.refresh : Theme.icons.download
                text: page.phase === "ready" ? qsTr("Restart and install")
                      : page.phase === "downloading" ? qsTr("Downloading… %1%").arg(Math.round(Updates.progress * 100))
                      : page.phase === "preparing" ? qsTr("Preparing…")
                      : Updates.canInstall ? qsTr("Download and install")
                      : qsTr("Open download page")
                enabled: page.phase === "available" || page.phase === "ready"
                onClicked: {
                    if (page.phase === "ready")
                        Updates.requestQuit()
                    else if (Updates.canInstall)
                        Updates.downloadAndInstall(false)
                    else
                        Updates.openDownloadPage()
                }
            }
        }
    }

    SettingsRow {
        title: qsTr("Check automatically")
        searchId: "updates-automatic"
        description: qsTr("Once a day, when Drift starts, look for a newer version and let you know.")
        keywords: "update automatic startup daily notify"
        available: Updates.supported
        query: page.query

        ThemedSwitch {
            checked: Updates.enabled
            onToggled: Updates.enabled = checked
        }
    }

    SettingsRow {
        title: qsTr("Portable copy")
        searchId: "updates-portable"
        description: qsTr("This copy of Drift was not installed, so it doesn't update itself. Install Drift with its Setup installer to get updates automatically, or download new versions from the releases page.")
        keywords: "update portable zip download releases"
        available: Updates.portable
        query: page.query

        ThemedButton {
            variant: "secondary"
            glyph: Theme.icons.externalLink
            text: qsTr("Open releases page")
            visible: Updates.releasesUrl.length > 0
            onClicked: Qt.openUrlExternally(Updates.releasesUrl)
        }
    }

    SettingsRow {
        title: qsTr("No update check")
        searchId: "updates-unavailable"
        description: qsTr("This build of Drift doesn't check for updates.")
        available: !Updates.supported && !Updates.portable
        query: page.query
    }
}
