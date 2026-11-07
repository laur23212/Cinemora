import QtQuick
import Drift

// Settings > Asset Providers: sign in once to the free asset sources the Stickers, Audio, Stock
// Video and Titles panels browse. Sign-ins are kept in the system's secure store and restored at
// startup until the user logs out.
SettingsPage {
    id: page

    pageId: "assetproviders"
    title: qsTr("Asset Providers")

    SettingsGroupHeader {
        text: qsTr("Accounts")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Free asset providers")
        searchId: "asset-providers-intro"
        description: qsTr("Connect free accounts to browse more stock video, music, sound effects, stickers and titles inside Cadra. "
                          + "You sign in once; Cadra remembers it until you log out.")
                     + " " + (OnlineAssets.credentialStorageEncrypted
                              ? qsTr("Sign-ins are stored in %1.").arg(OnlineAssets.credentialStorage)
                              : qsTr("Sign-ins are stored in %1 — not encrypted, but readable only by your user account.")
                                    .arg(OnlineAssets.credentialStorage))
        keywords: "asset providers accounts login api key pexels pixabay freesound jamendo openverse stock music stickers"
        query: page.query
        stacked: true

        Column {
            width: parent.width
            spacing: Theme.spacingMd

            Repeater {
                model: OnlineAssets.providers
                delegate: AssetProviderSettingsCard {
                    required property var modelData
                    width: parent.width
                    provider: modelData
                }
            }
        }
    }

    SettingsGroupHeader {
        text: qsTr("Downloads")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Keep downloaded assets")
        searchId: "asset-providers-keep"
        description: qsTr("Off: downloads are deleted when Cadra closes (anything a saved project uses is stored inside the project file). "
                          + "On: downloads are kept in your asset library and reused.")
        keywords: "keep downloads delete temporary cache library stock"
        query: page.query

        ThemedSwitch {
            checked: OnlineAssets.keepDownloads
            onToggled: OnlineAssets.keepDownloads = checked
        }
    }

    SettingsRow {
        title: qsTr("Custom sources")
        searchId: "asset-providers-catalog"
        description: qsTr("Add your own sources (any JSON search API, or lists of file links) in the catalog file, then reload.")
        keywords: "catalog json custom source lottie links"
        query: page.query

        Row {
            spacing: Theme.spacingSm
            ThemedButton {
                variant: "secondary"
                glyph: Theme.icons.pencil
                text: qsTr("Edit catalog")
                onClicked: OnlineAssets.openCatalogFile()
            }
            ThemedButton {
                variant: "ghost"
                glyph: Theme.icons.refresh
                text: qsTr("Reload")
                onClicked: OnlineAssets.reloadCatalog()
            }
        }
    }
}
