# Dafin

Dafin (Romanian for the laurel tree, the leaf in its icon) is a video editor for Windows and Linux based on
[Drift](https://github.com/CutWire-Studios/Drift) by CutWire Studios (GPL-3.0). It has a layout
familiar to Filmora users, a built-in Stock Media library and Cool Edit-style volume dots.
Dafin is not affiliated with Wondershare or CutWire Studios.

## What is different from Drift

| Area | Change | Files |
|---|---|---|
| Layout | New default workspace: library · info/properties · preview on top, timeline below | `resources/workspaces/filmora.json`, `WorkspaceStore.cpp` |
| Theme | Dark grey panels, teal accent, purple video / teal audio clips; default on first run | `resources/themes/filmora.json`, `ThemeManager.cpp` |
| Asset panel | Filmora-style icon+label category bar across the top (Media, Stock Media, Audio, Titles, Transitions, Effects, Stickers, Templates, then Drift's extras) | `AssetsPanelTopTabBar.qml`, `AssetsPanel.qml` |
| Online assets | Stock video, music, sound effects, stickers and title templates from free providers (Pexels, Pixabay, Openverse, Jamendo, Freesound, the bundled Dafin library) inside the asset panels, with previews, one-click download and Settings > Asset Providers for accounts | `src/app/online/library/`, `OnlineAssetBrowser.qml`, `asset-library/` |
| Lottie previews | Animated hover previews drawn by Drift's own Lottie renderer | `LottieFramePreviewImageProvider.*` |
| Volume dots | Cool Edit Pro-style volume line on every clip with sound: click the line to add a dot, drag dots to shape the volume (middle = 100 %, top = +6 dB, bottom = silent), Shift-drag for level only, double-click or right-click a dot to remove it. Uses Drift's volume keyframes, so playback and export follow it | `TimelineVolumeEnvelope.qml`, `TimelineTrackItem.cpp` |
| Name | Shown as Dafin everywhere (window, menus, About, installer). The About box keeps the credit to Drift. Settings live in their own "Dafin" folder. Settings and data from the app's earlier name, Cadra, move over on first start. On Windows the program is `dafin.exe` (the CMake target is still `drift`); the installer removes an old `drift.exe` | `PreferencesController.cpp` (BrandingTranslator), `main.cpp`, `RenameMigration.cpp`, `installer/windows/Drift.iss`, `CMakeLists.txt` (`OUTPUT_NAME`) |
| Icon | The "D" with a laurel leaf. Master is `Drift_icon.png`; `python3 tools/icons/generate_icons.py` regenerates the .ico, MSIX tiles and hicolor PNGs from it | `Drift_icon.png`, `resources/`, `installer/msix/Assets/`, `flatpak/share/icons/` |
| Updates | On Windows, an installed copy checks this repository's newest GitHub release once a day, says so once per version with a toast and keeps it in the bell, downloads the installer, checks its SHA-256 and runs it (UAC prompt), then restarts. Settings > Updates has **Check now**, **Download and install** / **Restart and install** and the daily-check switch; the gear menu has **Check for updates…**. A portable copy's Updates page points to the releases page instead. The version comes from the release tag, not upstream's DNS record. Portable copies and build trees (no `unins*.exe` beside the exe) never check | `UpdateChecker.cpp`, `UpdateAsset.cpp`, `SettingsUpdatesPage.qml`, `AppSettingsMenu.qml`, `Main.qml`, `CMakeLists.txt` (`DRIFT_UPDATE_FEED_URL`, `DAFIN_VERSION`), `.github/workflows/dafin-release.yml` |
| Your own files | Drag files onto a tab to add them: Lottie `.json`/`.lottie` onto Stickers ("My Lotties"), audio onto Audio ("My sounds"), `.mogrt` onto Titles (listed first in the main Titles grid, no separate category). **Save** keeps a file until **Delete**; unsaved ones go when Dafin closes. A quiet line at the bottom of each tab says so. The package button there, or Settings > Asset Providers > Your library, exports everything saved to one `.dafinlib` file and imports one on another computer | `UserLibrary.*`, `providers/UserLibraryProvider.*`, `OnlineSwitchTab.qml`, `OnlineAssetBrowser.qml`, `OnlineAssetCard.qml`, `SettingsAssetProvidersPage.qml`, `core/base/ZipArchive.*` (writer) |
| .mogrt titles | Name, text fields, length, poster and preview video are read from the template; clicking it adds an editable Dafin title with that text. The After Effects/Premiere animation inside can only be rendered by Adobe apps | `MogrtInfo.*`, `AppControllerDafin.cpp` (`addTemplateTitle`) |
| Keyframes | Filmora-style: a keyframe button beside Select and Cut mode (Alt+K) keys position, scale, rotation and opacity of the selected clip at the playhead, or removes the keyframe there. Clips show diamonds: click to seek, drag to retime, right-click to delete one or clear all. Once a property has keyframes, changing it at another time adds a keyframe there (Drift needed Auto keyframes for that) | `AppControllerDafin.cpp`, `TimelineKeyframeMarkers.qml`, `TimelineToolbarActions.qml`, `AppControllerClipProps.cpp`, `PreviewController.cpp` |
| Snapping | The snapping magnet sits with the edit tools after the keyframe button, as in Filmora, instead of at the right end (N toggles it) | `TimelineToolbarActions.qml`, `TimelineToolbar.qml`, `AppControllerTracks.cpp` |
| Cleanup | Online downloads go to a temp folder that is deleted when Dafin closes (unless "Keep downloaded assets" is on). Media a saved project uses is embedded in the `.drift` file, so projects never lose it | `SessionFiles.cpp`, `ProjectFileController.cpp` (captureSave) |
| Movable buttons | Press and hold any button on the top bar, under the preview or on the timeline toolbar, then drag it elsewhere in that bar; drop a timeline action on More to move it into the menu. Reset in Settings > Toolbars | `BarReorder*.qml`, `BarSlot.qml`, `PreferencesController` (`barLayouts`) |

To switch the asset panel back to Drift's vertical rail, set `assets/tabStyle` to `rail` (Settings
UI for it is still to do). The old layout is still available as the "Edit" workspace.

## Stock Media sources

The tab is driven by a JSON catalog. On first run Drift copies the built-in one to your app data
folder (`%APPDATA%\Dafin\Dafin\free-library\catalog.json` on Windows,
`~/.local/share/Dafin/Dafin/free-library/catalog.json` on Linux) — use **Edit catalog** in the tab to open
it, then **Reload catalog**. The `_help` block at the top of the file explains the format; any JSON
search API can be added without code changes.

Pexels, Pixabay and Freesound need a free API key each. The tab shows a **Get a free key** button
and a field to paste it; keys are stored in Drift's settings, not in the catalog.

Lottie: LottieFiles' search API needs a signed-in account, so it is not browsable here. On
lottiefiles.com open a free animation and either download the **Lottie JSON** file and drag it onto
the Stickers tab, or copy its URL and paste it into the tab's link box — it is downloaded and kept in
"My Lotties". (Links that older versions saved in the catalog move to My Lotties on first start.)

## Building the Windows installer with GitHub (no local toolchain)

1. Push this branch to your fork of Drift.
2. On GitHub: **Actions** → **Build** → **Run workflow**, pick this branch, platforms = `windows`.
3. When it finishes (first run ≈ 1 h because Skia and Qt are built/cached), download the
   `Dafin-Setup-*.exe` / portable zip from the run's **Artifacts**.

Linux: same workflow with `linux` (AppImage) or `flatpak`, or build locally per `docs/BUILDING.md`.

## Publishing an update

1. On GitHub: **Actions** → **Dafin release** → **Run workflow** on `dafin-edition`, with a version
   newer than the last release (e.g. `1.0.1`) and, optionally, what's new.
2. It builds the Windows installer and portable zip at that version and publishes release `v1.0.1`
   with both files and `SHA256SUMS`.
3. Installed copies find it within a day (or at once with Settings > Updates > Check now, or the gear
   menu's Check for updates…) and offer to install it. Portable copies do not update; copies installed before the updater existed need the
   first release installed by hand.

The Build workflow stays an unpublished dry run; its builds report the version in `CMakeLists.txt`.
