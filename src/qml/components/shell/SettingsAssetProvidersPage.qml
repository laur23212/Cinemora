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
        description: qsTr("Connect free accounts to browse more stock video, music, sound effects, stickers and titles inside Dafin. "
                          + "You sign in once; Dafin remembers it until you log out.")
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
        description: qsTr("Off: downloads are deleted when Dafin closes (anything a saved project uses is stored inside the project file). "
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

    // Dafin: the user's own Lotties, sounds and .mogrt titles, carried to another computer.
    SettingsGroupHeader {
        text: qsTr("Your library")
        query: page.query
    }

    SettingsRow {
        id: transferRow
        property string status: ""
        readonly property int savedCount: (OnlineAssets.userLibraryRevision, OnlineAssets.savedUserItemCount())
        title: qsTr("Move to another computer")
        searchId: "asset-providers-library-transfer"
        description: (transferRow.status.length > 0 ? transferRow.status + " " : "")
                     + qsTr("Export puts every Lottie, sound and title you saved into one .dafinlib file. "
                            + "In Dafin on the other computer, Import adds them, already saved.")
                     + " " + qsTr("%n item(s) saved now.", "", transferRow.savedCount)
        keywords: "export import backup migrate move library lotties sounds titles mogrt dafinlib"
        query: page.query

        Row {
            spacing: Theme.spacingSm
            ThemedButton {
                variant: "secondary"
                glyph: Theme.icons.folderOutput
                text: qsTr("Export…")
                enabled: transferRow.savedCount > 0
                onClicked: {
                    const day = new Date().toISOString().slice(0, 10)
                    const url = FileDialogs.saveFile(qsTr("Export my library"), [OnlineAssets.userLibraryArchiveFilter()],
                                                     qsTr("Dafin library %1").arg(day), ".dafinlib")
                    if (url && url.toString().length > 0)
                        transferRow.status = OnlineAssets.exportUserLibrary(url).message || ""
                }
            }
            ThemedButton {
                variant: "secondary"
                glyph: Theme.icons.folderInput
                text: qsTr("Import…")
                onClicked: {
                    const url = FileDialogs.openFile(qsTr("Import a Dafin library"),
                                                     [OnlineAssets.userLibraryArchiveFilter(), qsTr("All Files (*)")])
                    if (url && url.toString().length > 0)
                        transferRow.status = OnlineAssets.importUserLibrary(url).message || ""
                }
            }
        }
    }

    // Dafin: the free renderer that draws a .mogrt's After Effects animation (MogrtRenderer).
    SettingsRow {
        title: qsTr("MOGRT Converter")
        searchId: "asset-providers-mogrt-converter"
        description: MogrtRenderer.available
                     ? qsTr("Found: %1. Templates in Titles are added as template titles with their animation, and stay editable.")
                           .arg(MogrtRenderer.executable)
                     : qsTr("Not installed. With the free MOGRT Converter (installed separately, AGPL), .mogrt templates in Titles keep their After Effects animation and stay editable; without it they become ordinary Dafin titles.")
        keywords: "mogrt motion graphics template after effects render animation converter titles"
        query: page.query

        Row {
            spacing: Theme.spacingSm
            ThemedButton {
                variant: "secondary"
                glyph: Theme.icons.externalLink
                text: qsTr("Download")
                visible: !MogrtRenderer.available
                onClicked: Qt.openUrlExternally(MogrtRenderer.downloadUrl)
            }
            ThemedButton {
                variant: "secondary"
                glyph: Theme.icons.folderInput
                text: qsTr("Choose…")
                onClicked: {
                    const url = FileDialogs.openFile(qsTr("Choose MOGRT Converter"),
                                                     Qt.platform.os === "windows"
                                                         ? [qsTr("Programs (*.exe)"), qsTr("All Files (*)")]
                                                         : [qsTr("All Files (*)")])
                    if (url && url.toString().length > 0 && !MogrtRenderer.useProgram(url.toString()))
                        Toasts.error(qsTr("That file can't be run."))
                }
            }
            ThemedButton {
                variant: "ghost"
                glyph: Theme.icons.refresh
                text: qsTr("Check again")
                onClicked: MogrtRenderer.refresh()
            }
        }
    }

    // Dafin: renders of template titles (MogrtTitleController). Saved projects keep their own copy.
    SettingsRow {
        id: mogrtCacheRow
        property real megabytes: EditorState.mogrt.cacheMegabytes()
        title: qsTr("Rendered template titles")
        searchId: "asset-providers-mogrt-cache"
        description: qsTr("%1 MB of renders. Renders the open project doesn't use can be cleared; they are made again if needed, and saved projects carry their own copy. Renders unused for 45 days are cleared on their own.")
                         .arg(Math.round(megabytes))
        keywords: "mogrt cache renders disk space clear template titles"
        query: page.query

        ThemedButton {
            variant: "secondary"
            glyph: Theme.icons.trash
            text: qsTr("Clear unused")
            onClicked: {
                const removed = EditorState.mogrt.clearUnusedRenders()
                mogrtCacheRow.megabytes = EditorState.mogrt.cacheMegabytes()
                Toasts.info(qsTr("%n render(s) removed.", "", removed))
            }
        }
    }
}
