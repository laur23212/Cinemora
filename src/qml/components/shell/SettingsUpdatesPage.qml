import QtQuick
import QtQuick.Controls.Basic
import Drift

// Settings > Updates (Dafin). Dafin looks for a newer release on GitHub (laur23212/Dafin): the
// status line says whether this copy is up to date or an update is available, "Check for updates"
// asks right away, and the slider sets how often the automatic check runs. An installed copy
// downloads and installs a release from here — the installer is checked against its SHA-256, runs
// when Dafin restarts, and starts Dafin again. A portable copy is told about new versions too, but
// gets them from the releases page.
SettingsPage {
    id: page

    pageId: "updates"
    title: qsTr("Updates")

    readonly property string phase: Updates.downloading ? "downloading"
                                    : Updates.preparing ? "preparing"
                                    : Updates.readyToInstall ? "ready"
                                    : Updates.updateAvailable ? "available"
                                    : "idle"

    // Hours between automatic checks, the slider's stops. 0 is every start.
    readonly property var intervalStops: [0, 6, 12, 24, 72, 168, 336, 720]
    function intervalLabel(hours) {
        switch (hours) {
        case 0: return qsTr("Every time Dafin starts")
        case 6: return qsTr("Every 6 hours")
        case 12: return qsTr("Every 12 hours")
        case 24: return qsTr("Once a day")
        case 72: return qsTr("Every 3 days")
        case 168: return qsTr("Once a week")
        case 336: return qsTr("Every 2 weeks")
        case 720: return qsTr("Once a month")
        }
        return qsTr("Every %n hour(s)", "", hours)
    }
    function stopFor(hours) {
        let best = 0
        for (let i = 0; i < intervalStops.length; ++i) {
            if (Math.abs(intervalStops[i] - hours) < Math.abs(intervalStops[best] - hours))
                best = i
        }
        return best
    }

    // "Up to date" / "Update available" — the one line that answers the question.
    readonly property string statusText: Updates.checking ? qsTr("Checking for updates…")
                                         : Updates.updateAvailable ? qsTr("Update available: Dafin %1").arg(Updates.latestVersion)
                                         : Updates.error.length > 0 ? qsTr("Couldn't check for updates")
                                         : Updates.lastChecked.getTime && !isNaN(Updates.lastChecked.getTime()) ? qsTr("Up to date")
                                         : qsTr("Not checked yet")

    SettingsRow {
        title: qsTr("Installed version")
        searchId: "updates-version"
        description: qsTr("Dafin %1").arg(Updates.currentVersion)
        keywords: "version installed current"
        query: page.query

        ThemedButton {
            variant: "ghost"
            text: qsTr("What's new")
            visible: Updates.canCheck
            tooltip: qsTr("What changed in this version")
            onClicked: Updates.loadWhatsNew()
        }
    }

    SettingsRow {
        title: page.statusText
        searchId: "updates-check"
        description: {
            if (Updates.error.length > 0)
                return Updates.error
            const parts = []
            if (Updates.updateAvailable)
                parts.push(qsTr("A newer Dafin is out. You have %1.").arg(Updates.currentVersion))
            else if (Updates.status.length > 0 && !Updates.checking)
                parts.push(Updates.status)
            if (Updates.lastChecked.getTime && !isNaN(Updates.lastChecked.getTime()))
                parts.push(qsTr("Last checked %1.").arg(Updates.lastChecked.toLocaleString(Qt.locale(), Locale.ShortFormat)))
            if (parts.length === 0)
                parts.push(qsTr("Looks on GitHub for a newer Dafin."))
            return parts.join(" ")
        }
        keywords: "update check now version release newer download install github up to date available"
        available: Updates.canCheck
        query: page.query

        ThemedButton {
            variant: "secondary"
            glyph: Theme.icons.refresh
            text: Updates.checking ? qsTr("Checking…") : qsTr("Check for updates")
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
                return qsTr("Downloading Dafin %1…").arg(Updates.latestVersion)
            case "preparing":
                return qsTr("Preparing the update…")
            case "ready":
                return Updates.installScheduled
                        ? qsTr("Dafin %1 will install when you close Dafin.").arg(Updates.latestVersion)
                        : qsTr("Dafin %1 is downloaded. Restart Dafin to install it; you can save your work first.")
                              .arg(Updates.latestVersion)
            default:
                return Updates.canInstall
                        ? qsTr("Dafin %1: downloads the installer from GitHub and installs it when Dafin restarts.")
                              .arg(Updates.latestVersion)
                        : Updates.portable
                          ? qsTr("Dafin %1: this portable copy doesn't update itself. Download the new version from its release page.")
                                .arg(Updates.latestVersion)
                          : qsTr("Dafin %1: open the release page to download it.").arg(Updates.latestVersion)
            }
        }
        keywords: "update install download restart new version"
        available: Updates.canCheck && Updates.updateAvailable
        query: page.query

        Row {
            spacing: Theme.spacingSm

            ThemedButton {
                variant: "ghost"
                text: qsTr("Release notes")
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
        description: Updates.enabled
                     ? qsTr("%1, Dafin looks for a newer version and lets you know.").arg(page.intervalLabel(Updates.intervalHours))
                     : qsTr("Off. Use Check for updates to look for a newer version.")
        keywords: "update automatic startup daily notify"
        available: Updates.canCheck
        query: page.query

        ThemedSwitch {
            checked: Updates.enabled
            onToggled: Updates.enabled = checked
        }
    }

    SettingsRow {
        id: intervalRow
        title: qsTr("How often to check")
        searchId: "updates-interval"
        description: page.intervalLabel(page.intervalStops[Math.round(intervalSlider.value)])
        keywords: "update frequency interval how often hours daily weekly monthly startup slider"
        available: Updates.canCheck
        query: page.query

        ThemedSlider {
            id: intervalSlider
            width: Theme.c.settingsRow.controlWidth
            label: qsTr("How often to check for updates")
            enabled: Updates.enabled
            from: 0
            to: page.intervalStops.length - 1
            stepSize: 1
            snapMode: Slider.SnapAlways
            value: page.stopFor(Updates.intervalHours)
            valueFormatter: function (v) { return page.intervalLabel(page.intervalStops[Math.round(v)]) }
            onMoved: Updates.intervalHours = page.intervalStops[Math.round(value)]
        }
    }

    SettingsRow {
        title: qsTr("Portable copy")
        searchId: "updates-portable"
        description: qsTr("This copy of Dafin was not installed, so it can tell you about a new version but doesn't install it. Install Dafin with its Setup installer to get updates automatically, or download new versions from the releases page.")
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
        description: qsTr("This build of Dafin doesn't check for updates.")
        available: !Updates.canCheck
        query: page.query
    }
}
